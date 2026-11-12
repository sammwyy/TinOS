# =====================================================================
#  TinOS - top level build
#
#  make            build the kernel and the bootable ISO image
#  make run        build and launch TinOS in QEMU (graphical window)
#  make run-serial launch with the kernel log multiplexed onto stdout
#  make runvnc     launch QEMU serving the display over VNC (127.0.0.1:0)
#  make usb-image  create/reuse the FAT32 USB development image
#  make test       build and run the host-side unit tests
#  make check      host unit tests + automated QEMU boot/GUI and text tests
#  make clean      remove build products
# =====================================================================

.DEFAULT_GOAL := all

NAME      := tinos
BUILD     := build
KERNEL    := $(BUILD)/$(NAME).elf
ISO       := $(BUILD)/$(NAME).iso
MAP       := $(BUILD)/$(NAME).map
BOOT_IMAGE := $(BUILD)/tinos-boot.img
ISODIR    := $(BUILD)/iso-native

# Selects which kernel/arch/<ARCH> subtree gets built, e.g. `make ARCH=arm`.
# Only x86 exists today; CPU bring-up, page tables and the boot stub live
# under an <ARCH> directory so a future port adds a sibling tree instead of
# touching this one. Drivers live under kernel/drivers/<class>/; each
# architecture picks the ones it builds in kernel/arch/<ARCH>/drivers.mk.
ARCH      := x86
ifeq ($(wildcard kernel/arch/$(ARCH)),)
$(error unknown ARCH '$(ARCH)' - no kernel/arch/$(ARCH) directory)
endif
include kernel/arch/$(ARCH)/drivers.mk

# --- toolchain --------------------------------------------------------
CC        := gcc
LD        := ld
OBJCOPY   := objcopy

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
# kernel/core, sched, proc, fs, gfx, lib, mm, net and sh hold no
# arch-specific code - they build for every target. ARCH_DRIVERS (from
# kernel/arch/$(ARCH)/drivers.mk) selects files under kernel/drivers/.
# tests/kernel holds the in-kernel self-test.
# Everything under kernel/arch/$(ARCH) is that target's own: CPU/platform
# bring-up, page tables and the boot stub. The retired native-app ABI is not
# linked into the kernel; only the ELF32 userspace path can run app images.
SRCS_C := $(filter-out kernel/core/abi.c,$(sort kernel/main.c \
                  $(shell find kernel/core kernel/sched kernel/proc kernel/syscall \
                          kernel/fs kernel/gfx kernel/lib kernel/mm \
                          kernel/net kernel/sh kernel/tty tests/kernel \
                          desktop libraries/image libraries/audio -name '*.c') \
                  $(addprefix kernel/drivers/,$(ARCH_DRIVERS)) \
                  $(shell find kernel/arch/$(ARCH) -name '*.c')))
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
                      kernel/proc/elf.c \
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
# own kernel thread, and kernel/sched/thread.c saves and restores each
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

# --- isolated userspace and initrd -----------------------------------
# Only ring-3 ELF32 programs are included. Generated native-app artifacts
# from older builds are never read, even if they remain under build/native.
RAMFS := $(BUILD)/ramfs.tar
USERLAND_APPS := hello fault badptr echo fpu io df textwrite cat fdtest \
                 spin spawn spawn_badarg multi gui_security stream \
                 libprobe libtamper pcm_demo
GUI_APPS := about terminal explorer editor calculator sysinfo control_panel \
            image_viewer media_player
USERLAND_LIBS := libtinosbmp.so libtinoswav.so
SAMPLE_BMP := $(BUILD)/userland/media/demo.bmp
SAMPLE_WAV := $(BUILD)/userland/media/demo.wav

$(SAMPLE_BMP): tools/gen_sample_bmp.py
	@mkdir -p $(dir $@)
	python3 tools/gen_sample_bmp.py $@

$(SAMPLE_WAV): tools/gen_sample_wav.py
	@mkdir -p $(dir $@)
	python3 tools/gen_sample_wav.py $@

.PHONY: native-apps
native-apps:
	@echo "native ring-0 apps are disabled; port apps/ sources to userland/bin/" >&2
	@false

.PHONY: userland-apps
userland-apps:
	$(MAKE) -C libraries/libc
	$(MAKE) -C userland/lib
	$(MAKE) -C userland/bin

# --mtime/--owner/--group/--sort keep the archive's bytes independent of
# build-machine clock and user IDs, matching the ISO's own reproducibility
# guarantees below.
$(RAMFS): userland-apps $(SAMPLE_BMP) $(SAMPLE_WAV)
	@mkdir -p $(dir $@)
	@cd $(BUILD)/userland && tar --format=ustar --sort=name --mtime=@0 \
		--owner=0 --group=0 --numeric-owner -cf $(abspath $@) \
		$(addsuffix .elf,$(USERLAND_APPS)) \
		$(addprefix gui/,$(addsuffix .elf,$(GUI_APPS))) \
		$(addprefix lib/,$(USERLAND_LIBS)) \
		media/demo.bmp media/demo.wav

# --- BIOS El Torito ISO ------------------------------------------------
$(BOOT_IMAGE): $(KERNEL) $(RAMFS) boot/x86/loader.asm boot/x86/build_image.py
	python3 boot/x86/build_image.py $(KERNEL) $(RAMFS) boot/x86/loader.asm $@

$(ISO): $(BOOT_IMAGE)
	@mkdir -p $(ISODIR)/boot
	@cp $(BOOT_IMAGE) $(ISODIR)/boot/tinos-boot.img
	@find $(ISODIR) -exec touch -t 200001010000.00 {} +
	xorriso -as mkisofs -o $@ -volid TINOS \
		--modification-date=2000010100000000 \
		--set_all_file_dates 2000010100000000 \
		-b boot/tinos-boot.img -no-emul-boot -boot-load-size 4 \
		-boot-info-table $(ISODIR) >/dev/null 2>&1
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
	python3 tests/qemu/isolation_image_test.py --kernel $(KERNEL) --initrd $(RAMFS)
	tests/qemu/qemu_test.py --iso $(ISO)
	python3 tests/qemu/text_mode_test.py --iso $(ISO)
	python3 tests/qemu/text_mode_test.py --iso $(ISO) --mem 16
	python3 tests/qemu/text_mode_test.py --iso $(ISO) --mem 8 --no-desktop
	python3 tests/qemu/text_mode_test.py --iso $(ISO) --mem 4 --boot-default --no-desktop
	python3 tests/qemu/text_mode_test.py --iso $(ISO) --mem 16 --vga cirrus --boot-default --no-desktop --retry-desktop
	python3 tests/qemu/low_memory_gui_test.py --iso $(ISO)
	python3 tests/qemu/bootloader_test.py --iso $(ISO)
	python3 tests/qemu/user_process_test.py --iso $(ISO)
	python3 tests/qemu/process_preemption_test.py --iso $(ISO)
	python3 tests/qemu/process_preemption_test.py --iso $(ISO) --mem 8
	python3 tests/qemu/process_preemption_test.py --iso $(ISO) --mem 64 --cpus 2
	python3 tests/qemu/user_spawn_test.py --iso $(ISO)
	python3 tests/qemu/user_spawn_test.py --iso $(ISO) --mem 8
	python3 tests/qemu/dynamic_link_test.py --iso $(ISO) --mem 8
	python3 tests/qemu/dynamic_link_test.py --iso $(ISO) --mem 16
	python3 tests/qemu/pcm_test.py --iso $(ISO) --mem 8
	python3 tests/qemu/pcm_test.py --iso $(ISO) --mem 16

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
