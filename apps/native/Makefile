# Builds every native app under apps/native/<name>/ as its own freestanding
# ELF32 binary, separate from the main kernel build (see the top-level
# Makefile and kernel/core/elf.c for how they get loaded).
#
# Each app directory's .c files, plus the shared shim (apps/native/lib) and
# the two kernel library files that have no kernel-state dependency
# (string.c, printf.c), are compiled and linked against apps/native/link.ld.
# Everything an app needs from the kernel or the desktop beyond that goes
# through the ABI table in kernel/include/tinos/abi.h instead.

CC := gcc
LD := ld

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
BUILD ?= $(ROOT)/build/native

CFLAGS := -m32 -std=gnu11 -O2 -g \
          -ffreestanding -fno-builtin -fno-stack-protector -fno-common \
          -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
          -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
          -Wall -Wextra -Wno-unused-parameter \
          -I$(ROOT)/kernel/include -I$(ROOT)/desktop/include
LDFLAGS := -m elf_i386 -T $(ROOT)/apps/native/link.ld -nostdlib -z noexecstack \
           --build-id=none
LIBGCC  := $(shell $(CC) -m32 -print-libgcc-file-name)

SHARED_SRCS := $(ROOT)/apps/native/lib/shim.c \
               $(ROOT)/kernel/lib/string.c \
               $(ROOT)/kernel/lib/printf.c

APP_DIRS := $(patsubst %/,%,$(wildcard $(ROOT)/apps/native/*/))
APPS     := $(filter-out lib,$(notdir $(APP_DIRS)))
ELFS     := $(patsubst %,$(BUILD)/%.elf,$(APPS))

.PHONY: all clean
all: $(ELFS)

$(BUILD)/obj/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

define APP_template
APP_SRCS_$(1) := $$(wildcard $(ROOT)/apps/native/$(1)/*.c) $$(SHARED_SRCS)
APP_OBJS_$(1) := $$(patsubst $(ROOT)/%.c,$(BUILD)/obj/%.o,$$(APP_SRCS_$(1)))

$(BUILD)/$(1).elf: $$(APP_OBJS_$(1)) $(ROOT)/apps/native/link.ld
	@mkdir -p $(BUILD)
	$(LD) $(LDFLAGS) -o $$@ $$(APP_OBJS_$(1)) $(LIBGCC)
	@echo "  native app: $(1) ($$$$(stat -c%s $$@) bytes)"
endef

$(foreach a,$(APPS),$(eval $(call APP_template,$(a))))

clean:
	rm -rf $(BUILD)
