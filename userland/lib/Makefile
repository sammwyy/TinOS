CC := gcc
LD := ld
ROOT := $(abspath ../..)
BUILD := $(ROOT)/build/userland/lib
CFLAGS := -m32 -std=gnu11 -O2 -ffreestanding -fno-builtin \
          -fno-stack-protector -fPIC -fno-asynchronous-unwind-tables \
          -mno-sse -mno-sse2 -Wall -Wextra -Iinclude
LIBS := $(BUILD)/libtinosbmp.so $(BUILD)/libtinoswav.so \
        $(BUILD)/libtinostate.so $(BUILD)/libtinoschain.so

.PHONY: all
all: $(LIBS)

$(BUILD)/%.o: %.c Makefile
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/libtinosbmp.so: $(BUILD)/bmp.o Makefile
	$(LD) -m elf_i386 -shared -nostdlib -z noexecstack -z now \
		--hash-style=sysv --build-id=none -soname libtinosbmp.so \
		-o $@ $<

$(BUILD)/libtinoswav.so: $(BUILD)/wav.o Makefile
	$(LD) -m elf_i386 -shared -nostdlib -z noexecstack -z now \
		--hash-style=sysv --build-id=none -soname libtinoswav.so \
		-o $@ $<

$(BUILD)/libtinostate.so: $(BUILD)/state.o Makefile
	$(LD) -m elf_i386 -shared -nostdlib -z noexecstack -z now \
		--hash-style=gnu --build-id=none -soname libtinostate.so \
		-o $@ $<

$(BUILD)/libtinoschain.so: $(BUILD)/chain.o $(BUILD)/libtinostate.so Makefile
	$(LD) -m elf_i386 -shared -nostdlib -z noexecstack -z now \
		--hash-style=sysv --build-id=none -soname libtinoschain.so \
		-o $@ $(BUILD)/chain.o -L$(BUILD) -ltinostate
