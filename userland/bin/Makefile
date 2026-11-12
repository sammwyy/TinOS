CC := gcc
LD := ld
ROOT := $(abspath ../..)
BUILD := $(ROOT)/build/userland
LIBC_BUILD := $(ROOT)/build/libc
LIB_BUILD := $(ROOT)/build/userland/lib
LIBC_APPS := cat fdtest spin spawn spawn_badarg multi gui_security stream \
             libprobe libtamper pcm_demo chainopen chainclose tlsprobe gdprobe \
             gd_consumer_probe versionopen
LIBC_ELFS := $(addprefix $(BUILD)/,$(addsuffix .elf,$(LIBC_APPS)))
GUI_APPS := about terminal explorer editor calculator sysinfo control_panel \
            image_viewer media_player
GUI_ELFS := $(addprefix $(BUILD)/gui/,$(addsuffix .elf,$(GUI_APPS)))
DYNAMIC_ELFS := $(BUILD)/chainprobe.elf $(BUILD)/versionprobe.elf
DYNAMIC_APPS := image_viewer media_player
STATIC_GUI_APPS := $(filter-out $(DYNAMIC_APPS),$(GUI_APPS))
STATIC_GUI_ELFS := $(addprefix $(BUILD)/gui/,$(addsuffix .elf,$(STATIC_GUI_APPS)))
CFLAGS := -m32 -std=gnu11 -O2 -ffreestanding -fno-builtin \
          -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
          -mno-sse -mno-sse2 -Wall -Wextra -MMD -MP \
          -I$(ROOT)/kernel/include/uapi -I$(ROOT)/libraries/libc/include \
          -I$(ROOT)/userland/lib/include

.PHONY: all
all: $(BUILD)/hello.elf $(BUILD)/fault.elf $(BUILD)/badptr.elf \
     $(BUILD)/echo.elf $(BUILD)/fpu.elf $(BUILD)/io.elf \
     $(BUILD)/df.elf $(BUILD)/textwrite.elf $(LIBC_ELFS) $(GUI_ELFS) \
     $(DYNAMIC_ELFS)

$(LIBC_BUILD)/libtinos.a $(LIBC_BUILD)/crt0.o &: \
    $(ROOT)/libraries/libc/Makefile \
    $(wildcard $(ROOT)/libraries/libc/arch/x86/*)
	$(MAKE) -C $(ROOT)/libraries/libc

$(LIB_BUILD)/libtinosbmp.so $(LIB_BUILD)/libtinoswav.so \
$(LIB_BUILD)/libtinostate.so $(LIB_BUILD)/libtinoschain.so \
$(LIB_BUILD)/libtinostls.so $(LIB_BUILD)/libtinosrt.so \
$(LIB_BUILD)/libtinosgd.so $(LIB_BUILD)/libtinosconsumer.so \
$(LIB_BUILD)/libtinosversioned.so $(LIB_BUILD)/libtinosversionconsumer.so &: \
    $(ROOT)/userland/lib/Makefile $(wildcard $(ROOT)/userland/lib/*.c) \
    $(wildcard $(ROOT)/userland/lib/*.map) \
    $(wildcard $(ROOT)/userland/lib/include/tinos/*.h)
	$(MAKE) -C $(ROOT)/userland/lib

$(BUILD)/%.o: %.c Makefile
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

$(STATIC_GUI_ELFS): $(BUILD)/gui/%.elf: $(BUILD)/%.o $(LIBC_BUILD)/crt0.o \
                              $(LIBC_BUILD)/libtinos.a link.ld
	@mkdir -p $(BUILD)/gui
	$(LD) -m elf_i386 -nostdlib -z noexecstack --build-id=none \
		-T link.ld -o $@ $(LIBC_BUILD)/crt0.o $< \
		$(LIBC_BUILD)/libtinos.a

$(BUILD)/gui/image_viewer.elf: $(BUILD)/image_viewer.o \
    $(LIBC_BUILD)/crt0.o $(LIBC_BUILD)/libtinos.a \
    $(LIB_BUILD)/libtinosbmp.so link.ld
	@mkdir -p $(BUILD)/gui
	$(LD) -m elf_i386 -nostdlib -z noexecstack -z now --build-id=none \
		--no-dynamic-linker --hash-style=sysv -T link.ld -o $@ \
		$(LIBC_BUILD)/crt0.o $(BUILD)/image_viewer.o \
		$(LIBC_BUILD)/libtinos.a -L$(LIB_BUILD) -ltinosbmp

$(BUILD)/gui/media_player.elf: $(BUILD)/media_player.o \
    $(LIBC_BUILD)/crt0.o $(LIBC_BUILD)/libtinos.a \
    $(LIB_BUILD)/libtinoswav.so link.ld
	@mkdir -p $(BUILD)/gui
	$(LD) -m elf_i386 -nostdlib -z noexecstack -z now --build-id=none \
		--no-dynamic-linker --hash-style=sysv -T link.ld -o $@ \
		$(LIBC_BUILD)/crt0.o $(BUILD)/media_player.o \
		$(LIBC_BUILD)/libtinos.a -L$(LIB_BUILD) -ltinoswav

$(BUILD)/chainprobe.elf: $(BUILD)/chainprobe.o $(LIBC_BUILD)/crt0.o \
    $(LIBC_BUILD)/libtinos.a $(LIB_BUILD)/libtinoschain.so link.ld Makefile
	$(LD) -m elf_i386 -nostdlib -z noexecstack -z now --build-id=none \
		--no-dynamic-linker --hash-style=gnu -rpath-link $(LIB_BUILD) \
		-T link.ld -o $@ \
		$(LIBC_BUILD)/crt0.o $(BUILD)/chainprobe.o \
		$(LIBC_BUILD)/libtinos.a -L$(LIB_BUILD) -ltinoschain

$(BUILD)/versionprobe.elf: $(BUILD)/versionprobe.o $(LIBC_BUILD)/crt0.o \
    $(LIBC_BUILD)/libtinos.a $(LIB_BUILD)/libtinosversioned.so link.ld Makefile
	$(LD) -m elf_i386 -nostdlib -z noexecstack -z now --build-id=none \
		--no-dynamic-linker --hash-style=gnu -rpath-link $(LIB_BUILD) \
		-T link.ld -o $@ \
		$(LIBC_BUILD)/crt0.o $(BUILD)/versionprobe.o \
		$(LIBC_BUILD)/libtinos.a -L$(LIB_BUILD) -ltinosversioned

-include $(wildcard $(BUILD)/*.d)
