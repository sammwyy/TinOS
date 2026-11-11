# Builds every app under apps/<name>/ as its own freestanding ELF32
# binary, separate from the main kernel build (see the top-level Makefile
# and kernel/proc/elf.c for how they get loaded).
#
# Each app directory's .c files, plus the shared shim (apps/lib) and the
# kernel library files that have no kernel-state dependency (string.c,
# printf.c, ...), are compiled and linked against apps/link.ld. Everything
# an app needs from the kernel or the desktop beyond that goes through the
# ABI table in kernel/include/tinos/abi.h instead.

CC := gcc
LD := ld

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
BUILD ?= $(ROOT)/build/native

CFLAGS := -m32 -std=gnu11 -O2 -g \
          -ffreestanding -fno-builtin -fno-stack-protector -fno-common \
          -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
          -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
          -Wall -Wextra -Wno-unused-parameter \
          -I$(ROOT)/kernel/include -I$(ROOT)/desktop/include \
          -I$(ROOT)/libraries/image/include -I$(ROOT)/libraries/audio/include
LDFLAGS := -m elf_i386 -T $(ROOT)/apps/link.ld -nostdlib -z noexecstack \
           --build-id=none
LIBGCC  := $(shell $(CC) -m32 -print-libgcc-file-name)

# Every one of these depends only on heap.h (already ABI-forwarded) or on
# nothing kernel-related at all, so they compile straight into every app
# instead of going through the ABI - unlike gfx.c/window.c/fs code, they
# hold no state that has to be the single kernel-wide copy.
SHARED_SRCS := $(ROOT)/apps/lib/shim.c \
               $(ROOT)/kernel/lib/string.c \
               $(ROOT)/kernel/lib/printf.c \
               $(ROOT)/kernel/fs/path.c \
               $(ROOT)/kernel/core/spinlock.c \
               $(ROOT)/kernel/sh/stream.c

APP_DIRS := $(patsubst %/,%,$(wildcard $(ROOT)/apps/*/))
APPS     := $(filter-out lib,$(notdir $(APP_DIRS)))
ELFS     := $(foreach a,$(APPS),$(BUILD)/$(a)/$(a).elf)

# One fixed, non-overlapping 128 KiB slot per app inside the arena
# kernel/include/tinos/native_app.h reserves (NATIVE_APP_BASE upward);
# --defsym feeds the chosen address into link.ld's NATIVE_APP_LOAD_ADDR.
# A new app just needs a new line here, at the next free 0x00020000 step.
NATIVE_APP_SLOT_calculator    := 0x00800000
NATIVE_APP_SLOT_control_panel := 0x00820000
NATIVE_APP_SLOT_editor        := 0x00840000
NATIVE_APP_SLOT_explorer      := 0x00860000
NATIVE_APP_SLOT_image_viewer  := 0x00880000
NATIVE_APP_SLOT_media_player  := 0x008a0000
NATIVE_APP_SLOT_terminal      := 0x008c0000
NATIVE_APP_SLOT_about         := 0x008e0000
NATIVE_APP_SLOT_sysinfo       := 0x00900000

.PHONY: all clean
all: $(ELFS)

$(BUILD)/obj/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Each app's package directory carries its ELF alongside the two files
# kernel/main.c reads once it is unpacked from the ramfs: app.conf (name,
# summary, category - see kernel/main.c's parse_manifest()) and an
# optional icon.png (see load_icon()). Bundling them here, next to the
# binary they describe, means a new app is exactly one new apps/<name>/
# directory - nothing to register in the top-level Makefile.
define APP_template
APP_SRCS_$(1) := $$(wildcard $(ROOT)/apps/$(1)/*.c) $$(SHARED_SRCS)
APP_OBJS_$(1) := $$(patsubst $(ROOT)/%.c,$(BUILD)/obj/%.o,$$(APP_SRCS_$(1)))

ifeq ($$(NATIVE_APP_SLOT_$(1)),)
$$(error no NATIVE_APP_SLOT_$(1) address - add one next to the others in apps/Makefile)
endif

$(BUILD)/$(1)/$(1).elf: $$(APP_OBJS_$(1)) $(ROOT)/apps/link.ld \
                        $(ROOT)/apps/$(1)/app.conf $(ROOT)/apps/Makefile
	@mkdir -p $(BUILD)/$(1)
	$(LD) $(LDFLAGS) --defsym=NATIVE_APP_LOAD_ADDR=$$(NATIVE_APP_SLOT_$(1)) \
		--defsym=NATIVE_APP_SLOT_SIZE=0x00020000 \
		-o $$@ $$(APP_OBJS_$(1)) $(LIBGCC)
	@cp $(ROOT)/apps/$(1)/app.conf $(BUILD)/$(1)/app.conf
	@if [ -f $(ROOT)/apps/$(1)/icon.png ]; then \
		cp $(ROOT)/apps/$(1)/icon.png $(BUILD)/$(1)/icon.png; \
	fi
	@echo "  native app: $(1) ($$$$(stat -c%s $$@) bytes)"
endef

$(foreach a,$(APPS),$(eval $(call APP_template,$(a))))

clean:
	rm -rf $(BUILD)
