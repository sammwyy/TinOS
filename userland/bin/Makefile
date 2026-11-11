CC := gcc
LD := ld
ROOT := $(abspath ../..)
BUILD := $(ROOT)/build/userland
CFLAGS := -m32 -std=gnu11 -O2 -ffreestanding -fno-builtin \
          -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
          -mno-sse -mno-sse2 -Wall -Wextra -I$(ROOT)/kernel/include/uapi

.PHONY: all
all: $(BUILD)/hello.elf $(BUILD)/fault.elf $(BUILD)/badptr.elf \
     $(BUILD)/echo.elf $(BUILD)/fpu.elf $(BUILD)/io.elf \
     $(BUILD)/df.elf $(BUILD)/textwrite.elf

$(BUILD)/%.o: %.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.elf: $(BUILD)/%.o link.ld
	$(LD) -m elf_i386 -nostdlib -z noexecstack --build-id=none \
		-T link.ld -o $@ $<
