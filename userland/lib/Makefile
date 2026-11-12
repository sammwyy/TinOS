CC := gcc
LD := ld
ROOT := $(abspath ../..)
BUILD := $(ROOT)/build/userland/lib
CFLAGS := -m32 -std=gnu11 -O2 -ffreestanding -fno-builtin \
          -fno-stack-protector -fPIC -fno-asynchronous-unwind-tables \
          -mno-sse -mno-sse2 -Wall -Wextra -Iinclude \
          -I$(ROOT)/kernel/include/uapi
LIBS := $(BUILD)/libtinosbmp.so $(BUILD)/libtinoswav.so \
        $(BUILD)/libtinostate.so $(BUILD)/libtinoschain.so \
        $(BUILD)/libtinostls.so $(BUILD)/libtinosrt.so \
        $(BUILD)/libtinosgd.so $(BUILD)/libtinosconsumer.so \
        $(BUILD)/libtinosversioned.so $(BUILD)/libtinosversionconsumer.so \
        $(BUILD)/libtinoslarge.so

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

$(BUILD)/tls.o: tls.c Makefile
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -ftls-model=initial-exec -c $< -o $@

$(BUILD)/libtinostls.so: $(BUILD)/tls.o Makefile
	$(LD) -m elf_i386 -shared -nostdlib -z noexecstack -z now \
		--hash-style=gnu --build-id=none -soname libtinostls.so \
		-o $@ $<

$(BUILD)/libtinosrt.so: $(BUILD)/tls_runtime.o Makefile
	$(LD) -m elf_i386 -shared -nostdlib -z noexecstack -z now \
		--hash-style=gnu --build-id=none -soname libtinosrt.so \
		-o $@ $<

$(BUILD)/libtinosgd.so: $(BUILD)/gd.o $(BUILD)/libtinosrt.so Makefile
	$(LD) -m elf_i386 -shared -nostdlib -z noexecstack -z now \
		--hash-style=gnu --build-id=none -soname libtinosgd.so \
		-o $@ $(BUILD)/gd.o -L$(BUILD) -ltinosrt

$(BUILD)/libtinosconsumer.so: $(BUILD)/gd_consumer.o \
    $(BUILD)/libtinosgd.so $(BUILD)/libtinosrt.so Makefile
	$(LD) -m elf_i386 -shared -nostdlib -z noexecstack -z now \
		--hash-style=gnu --build-id=none -soname libtinosconsumer.so \
		-o $@ $(BUILD)/gd_consumer.o -L$(BUILD) -ltinosgd -ltinosrt

$(BUILD)/libtinosversioned.so: $(BUILD)/versioned.o versioned.map Makefile
	$(LD) -m elf_i386 -shared -nostdlib -z noexecstack -z now \
		--hash-style=gnu --build-id=none -soname libtinosversioned.so \
		--version-script=versioned.map -o $@ $(BUILD)/versioned.o

$(BUILD)/libtinosversionconsumer.so: $(BUILD)/version_consumer.o \
    $(BUILD)/libtinosversioned.so Makefile
	$(LD) -m elf_i386 -shared -nostdlib -z noexecstack -z now \
		--hash-style=gnu --build-id=none \
		-soname libtinosversionconsumer.so -o $@ \
		$(BUILD)/version_consumer.o -L$(BUILD) -ltinosversioned

$(BUILD)/libtinoslarge.so: $(BUILD)/large.o Makefile
	$(LD) -m elf_i386 -shared -nostdlib -z noexecstack -z now \
		--hash-style=gnu --build-id=none -soname libtinoslarge.so \
		-o $@ $<
