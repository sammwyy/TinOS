# =====================================================================
#  TinOS - top level build
#
#  make            build the kernel and the bootable ISO image
#  make run        build and launch TinOS in QEMU (graphical window)
#  make run-serial launch with the kernel log multiplexed onto stdout
#  make runvnc     launch QEMU serving the display over VNC (127.0.0.1:0)
#  make usb-image  create/reuse the FAT32 USB development image
#  make test       build and run the host-side unit tests
#  make check      host unit tests + automated QEMU boot/GUI test
#  make clean      remove build products
# =====================================================================

NAME      := tinos
BUILD     := build
ISODIR    := $(BUILD)/isodir
KERNEL    := $(BUILD)/$(NAME).elf
ISO       := $(BUILD)/$(NAME).iso
MAP       := $(BUILD)/$(NAME).map

# Selects which kernel/arch/<ARCH> and kernel/dev/<ARCH> subtree gets built,
# e.g. `make ARCH=arm`. Only x86 exists today; everything platform-specific
# (CPU bring-up, page tables, the boot stub, and every driver that speaks
# port I/O or PCI) lives under an <ARCH> directory so a future port adds a
# sibling tree instead of touching this one.
ARCH      := x86
ifeq ($(wildcard kernel/arch/$(ARCH)),)
$(error unknown ARCH '$(ARCH)' - no kernel/arch/$(ARCH) directory)
endif

# --- toolchain --------------------------------------------------------
CC        := gcc
LD        := ld
OBJCOPY   := objcopy
GRUB_ISO  := $(shell command -v grub2-mkrescue 2>/dev/null || command -v grub-mkrescue 2>/dev/null)

# Each self-contained unit (kernel core, the desktop/window manager, and the
# image/audio format libraries) owns its own public headers under its own
# include/tinos/ tree, so pulling one out only means dropping its -I flag.
INCLUDES := -Ikernel/include -Idesktop/include \
            -Ilibraries/image/include -Ilibraries/audio/include

# Freestanding 32-bit code: no libc, no SSE/MMX (we never save FPU state),
# no PIC, and no stack protector.  -mgeneral-regs-only keeps the compiler
# from using vector registers inside interrupt paths.
CFLAGS := -m32 -std=gnu11 -O2 -g \
          -ffreestanding -fno-builtin -fno-stack-protector -fno-common \
          -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
          -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mgeneral-regs-only \
          -Wall -Wextra -Wno-unused-parameter -Wno-address-of-packed-member \
          $(INCLUDES) -I$(BUILD)/generated
ASFLAGS := -m32 -g -Ikernel/include
LDFLAGS := -m elf_i386 -T boot/linker.ld -nostdlib -z noexecstack \
           --build-id=none -Map=$(MAP)
LIBGCC  := $(shell $(CC) -m32 -print-libgcc-file-name)

# --- sources ----------------------------------------------------------
# kernel/core, fs, gfx, lib, mm, net and sh hold no arch-specific code -
# they build for every target. kernel/dev/*.c (flat, not in an <ARCH>
# subdir) is likewise transport-agnostic (e.g. usb_msc.c). Everything
# under kernel/arch/$(ARCH) and kernel/dev/$(ARCH) is that target's own:
# CPU/platform bring-up, page tables, the boot stub, and drivers tied to
# that platform's bus/IO model.
SRCS_C := $(sort kernel/main.c \
                  $(shell find kernel/core kernel/fs kernel/gfx kernel/lib \
                          kernel/mm kernel/net kernel/sh apps desktop \
                          libraries -name '*.c') \
                  $(wildcard kernel/dev/*.c) \
                  $(shell find kernel/dev/$(ARCH) kernel/arch/$(ARCH) \
                          -name '*.c'))
SRCS_S := $(sort $(shell find kernel/arch/$(ARCH) -name '*.S'))
OBJS   := $(patsubst %.c,$(BUILD)/%.c.o,$(SRCS_C)) \
          $(patsubst %.S,$(BUILD)/%.S.o,$(SRCS_S))
DEPS   := $(OBJS:.o=.d)
ICON_GEN_C := $(BUILD)/generated/icons_generated.c
ICON_GEN_H := $(BUILD)/generated/icons_generated.h
ICON_GEN_OBJ := $(BUILD)/generated/icons_generated.c.o
ICON_GEN_STAMP := $(BUILD)/generated/.icons.stamp
FORCE:

# --- host unit tests --------------------------------------------------
HOST_SRCS   := $(sort $(wildcard kernel/fs/*.c) $(wildcard kernel/sh/*.c) \
                      libraries/audio/codec.c libraries/audio/wav.c \
                      libraries/audio/vorbis.c kernel/lib/printf.c \
                      $(wildcard tests/host/*.c))
HOST_BIN    := $(BUILD)/tests/tinos-tests
# AddressSanitizer catches filesystem/parser memory bugs on the host, but the
# runtime is not installed everywhere, so probe for it.
HOST_SAN := $(shell echo 'int main(void){return 0;}' | \
              $(CC) -fsanitize=address,undefined -x c - -o /dev/null \
              >/dev/null 2>&1 && echo -fsanitize=address,undefined)
HOST_CFLAGS := -std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter \
               -DTINOS_HOST=1 -Ikernel/include -Ilibraries/audio/include \
               -Itests/host $(HOST_SAN)

.PHONY: all iso kernel resources run run-serial run-headless runvnc usb-image test check verify-reproducible clean distclean

all: $(ISO)

$(ICON_GEN_STAMP): tools/gen_icons.py $(wildcard resources/icons/*.bmp)
	@mkdir -p $(dir $@)
	python3 tools/gen_icons.py resources/icons $(ICON_GEN_C)
	@touch $@

# The generator writes both outputs in one pass; the stamp gives make one
# concrete dependency and avoids races between the header and source outputs.
$(ICON_GEN_C) $(ICON_GEN_H): $(ICON_GEN_STAMP)
	@test -f $@

resources: $(ICON_GEN_STAMP)

$(ICON_GEN_OBJ): $(ICON_GEN_C) $(ICON_GEN_H)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Every kernel translation unit may include the generated public UI header;
# make this ordering explicit for parallel clean builds.
$(OBJS): $(ICON_GEN_H)

# --- kernel -----------------------------------------------------------
$(BUILD)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Image/audio decode never runs inside an interrupt handler - it runs on its
# own kernel thread, and kernel/core/thread.c saves and restores each
# thread's FXSAVE state (x87 + SSE) on every context switch. So unlike the
# rest of the kernel, these translation units get real SSE2 codegen and
# auto-vectorization instead of the interrupt-safe scalar baseline.
# -mstackrealign is required because the hand-written thread bootstrap/
# resume paths in arch/x86 do not themselves guarantee the 16-byte stack
# alignment SSE's aligned loads/stores assume.
CODEC_CFLAGS := $(filter-out -mgeneral-regs-only -mno-sse -mno-sse2,$(CFLAGS)) \
                -msse2 -mstackrealign -ftree-vectorize
$(BUILD)/libraries/audio/%.c.o: CFLAGS := $(CODEC_CFLAGS)
# jpeg/png/webp decode has no reason to keep x87's excess precision and
# stack-juggling; audio keeps the default (387) so vorbis.c's x87 transform
# math, noted below, is unaffected.
$(BUILD)/libraries/image/%.c.o: CFLAGS := $(CODEC_CFLAGS) -mfpmath=sse

# stb_vorbis additionally wants the x87 stack specifically (not SSE scalar
# math) for its portable transform math; CODEC_CFLAGS above already dropped
# -mgeneral-regs-only for the whole libraries/audio/ directory, so nothing
# further is needed here beyond documenting why the file is exempt from
# -mfpmath=sse.

$(BUILD)/%.S.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -MMD -MP -c $< -o $@

$(KERNEL): $(OBJS) $(ICON_GEN_OBJ) $(ICON_GEN_STAMP) boot/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(ICON_GEN_OBJ) $(LIBGCC)
	@echo "  kernel: $$(stat -c%s $@) bytes"

kernel: $(KERNEL)

# --- bootable ISO -----------------------------------------------------
# -d .../i386-pc keeps the image BIOS-only (no EFI payload), which is what
# QEMU's default machine boots and keeps the ISO under 5 MiB.
#
# Getting a byte-identical ISO out of grub2-mkrescue takes three passes:
#   1. -volume_date pins the primary volume descriptor's own creation/
#      modification fields and the per-file Rock Ridge dates for anything
#      copied from ISODIR (the "touch -t 2000..." below only fixes mtime,
#      which is all -volume_date all_file_dates reads back).
#   2. A second xorriso pass rewrites those same fields on the assembled
#      image; "-boot_image any patch" keeps the existing El Torito boot
#      catalog instead of regenerating (and thereby randomising) it.
#   3. grub2-mkrescue's own module files still carry one field neither pass
#      above can reach: the Rock Ridge "attribute change" timestamp, sourced
#      from the real on-disk ctime of files copied into grub2-mkrescue's own
#      scratch directory, which touch(1) cannot set. tools/pin_iso_dates.py
#      patches that field directly in the finished image.
GRUB_BIOS_DIR := $(firstword $(wildcard /usr/lib/grub/i386-pc \
                                        /usr/share/grub/i386-pc))
GRUB_ISO_ARGS := $(if $(GRUB_BIOS_DIR),-d $(GRUB_BIOS_DIR),)

$(ISO): $(KERNEL) boot/grub.cfg tools/pin_iso_dates.py
ifeq ($(GRUB_ISO),)
	$(error grub2-mkrescue/grub-mkrescue not found - install grub2-tools-extra and xorriso)
endif
	@rm -rf $(ISODIR)
	@mkdir -p $(ISODIR)/boot/grub
	@cp $(KERNEL) $(ISODIR)/boot/$(NAME).elf
	@cp boot/grub.cfg $(ISODIR)/boot/grub/grub.cfg
	@find $(ISODIR) -exec touch -t 200001010000.00 {} +
	$(GRUB_ISO) $(GRUB_ISO_ARGS) --compress=xz -o $@ $(ISODIR) \
		-- -volid TINOS -volume_date all_file_dates =0 \
		   -volume_date uuid 2000010100000000 \
		   -volume_date c 2000010100000000 \
		   -volume_date m 2000010100000000 >/dev/null 2>&1
	@xorriso -indev $@ -boot_image any patch -outdev $@.tmp \
		-volume_date c 2000010100000000 -volume_date m 2000010100000000 \
		-alter_date_r b 2000010100000000 / -- -commit >/dev/null 2>&1
	@mv $@.tmp $@
	@python3 tools/pin_iso_dates.py $@ >/dev/null
	@echo "  image : $@ ($$(stat -c%s $@) bytes)"

iso: $(ISO)

# --- running ----------------------------------------------------------
run: $(ISO)
	tools/run.sh

run-serial: $(ISO)
	tools/run.sh --serial-stdio

run-headless: $(ISO)
	tools/run.sh --headless

# Serve the display over VNC instead of opening a local window (VNC := address)
runvnc: $(ISO)
	tools/run.sh --vnc

usb-image:
	tools/make_usb_image.sh

# --- tests ------------------------------------------------------------
$(HOST_BIN): $(HOST_SRCS) $(wildcard kernel/include/tinos/*.h) \
             $(wildcard libraries/audio/include/tinos/*.h) $(wildcard tests/host/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(HOST_SRCS) -o $@

test: $(HOST_BIN)
	@$(HOST_BIN)

check: test $(ISO)
	tools/qemu_test.py --iso $(ISO)

# Build the kernel and ISO twice from a clean tree and compare the results.
verify-reproducible:
	@$(MAKE) --no-print-directory clean >/dev/null
	@$(MAKE) --no-print-directory $(ISO) >/dev/null
	@kfirst=$$(sha256sum $(KERNEL) | cut -d' ' -f1); \
	 ifirst=$$(sha256sum $(ISO) | cut -d' ' -f1); \
	 sleep 2; \
	 $(MAKE) --no-print-directory clean >/dev/null; \
	 $(MAKE) --no-print-directory $(ISO) >/dev/null; \
	 ksecond=$$(sha256sum $(KERNEL) | cut -d' ' -f1); \
	 isecond=$$(sha256sum $(ISO) | cut -d' ' -f1); \
	 if [ "$$kfirst" = "$$ksecond" ] && [ "$$ifirst" = "$$isecond" ]; then \
	     echo "reproducible: two clean builds produced an identical kernel and ISO"; \
	     echo "  kernel sha256 $$kfirst"; \
	     echo "  iso    sha256 $$ifirst"; \
	 else \
	     echo "NOT reproducible:"; \
	     echo "  kernel $$kfirst / $$ksecond"; \
	     echo "  iso    $$ifirst / $$isecond"; \
	     exit 1; \
	 fi

clean:
	rm -rf $(BUILD)

distclean: clean

-include $(DEPS)
