CC := gcc
LD := ld
ROOT := $(abspath ../..)
BUILD := $(ROOT)/build/userland
LIBC_BUILD := $(ROOT)/build/libc
LIBC_APPS := cat fdtest spin spawn spawn_badarg multi gui_security stream
LIBC_ELFS := $(addprefix $(BUILD)/,$(addsuffix .elf,$(LIBC_APPS)))
GUI_APPS := about terminal explorer editor calculator sysinfo control_panel \
            image_viewer media_player
GUI_ELFS := $(addprefix $(BUILD)/gui/,$(addsuffix .elf,$(GUI_APPS)))
CFLAGS := -m32 -std=gnu11 -O2 -ffreestanding -fno-builtin \
          -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
          -mno-sse -mno-sse2 -Wall -Wextra -MMD -MP \
          -I$(ROOT)/kernel/include/uapi -I$(ROOT)/libraries/libc/include

.PHONY: all
all: $(BUILD)/hello.elf $(BUILD)/fault.elf $(BUILD)/badptr.elf \
     $(BUILD)/echo.elf $(BUILD)/fpu.elf $(BUILD)/io.elf \
     $(BUILD)/df.elf $(BUILD)/textwrite.elf $(LIBC_ELFS) $(GUI_ELFS)

$(LIBC_BUILD)/libtinos.a $(LIBC_BUILD)/crt0.o &: \
    $(ROOT)/libraries/libc/Makefile \
    $(wildcard $(ROOT)/libraries/libc/arch/x86/*)
	$(MAKE) -C $(ROOT)/libraries/libc

$(BUILD)/%.o: %.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.elf: $(BUILD)/%.o link.ld
	$(LD) -m elf_i386 -nostdlib -z noexecstack --build-id=none \
		-T link.ld -o $@ $<

$(LIBC_ELFS): $(BUILD)/%.elf: $(BUILD)/%.o $(LIBC_BUILD)/crt0.o \
                              $(LIBC_BUILD)/libtinos.a link.ld
	$(LD) -m elf_i386 -nostdlib -z noexecstack --build-id=none \
		-T link.ld -o $@ $(LIBC_BUILD)/crt0.o $< \
		$(LIBC_BUILD)/libtinos.a

$(GUI_ELFS): $(BUILD)/gui/%.elf: $(BUILD)/%.o $(LIBC_BUILD)/crt0.o \
                              $(LIBC_BUILD)/libtinos.a link.ld
	@mkdir -p $(BUILD)/gui
	$(LD) -m elf_i386 -nostdlib -z noexecstack --build-id=none \
		-T link.ld -o $@ $(LIBC_BUILD)/crt0.o $< \
		$(LIBC_BUILD)/libtinos.a

-include $(wildcard $(BUILD)/*.d)
