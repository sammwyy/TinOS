CC := gcc
LD := ld
ROOT := $(abspath ../..)
BUILD := $(ROOT)/build/userland
LIBC_BUILD := $(ROOT)/build/libc
CFLAGS := -m32 -std=gnu11 -O2 -ffreestanding -fno-builtin \
          -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
          -mno-sse -mno-sse2 -Wall -Wextra \
          -I$(ROOT)/kernel/include/uapi -I$(ROOT)/libraries/libc/include

.PHONY: all
all: $(BUILD)/hello.elf $(BUILD)/fault.elf $(BUILD)/badptr.elf \
     $(BUILD)/echo.elf $(BUILD)/fpu.elf $(BUILD)/io.elf \
     $(BUILD)/df.elf $(BUILD)/textwrite.elf $(BUILD)/cat.elf \
     $(BUILD)/fdtest.elf

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

$(BUILD)/cat.elf: $(BUILD)/cat.o $(LIBC_BUILD)/crt0.o \
                  $(LIBC_BUILD)/libtinos.a link.ld
	$(LD) -m elf_i386 -nostdlib -z noexecstack --build-id=none \
		-T link.ld -o $@ $(LIBC_BUILD)/crt0.o $< \
		$(LIBC_BUILD)/libtinos.a

$(BUILD)/fdtest.elf: $(BUILD)/fdtest.o $(LIBC_BUILD)/crt0.o \
                     $(LIBC_BUILD)/libtinos.a link.ld
	$(LD) -m elf_i386 -nostdlib -z noexecstack --build-id=none \
		-T link.ld -o $@ $(LIBC_BUILD)/crt0.o $< \
		$(LIBC_BUILD)/libtinos.a
