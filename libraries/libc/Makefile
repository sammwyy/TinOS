CC := gcc
AR := ar
ROOT := $(abspath ../..)
BUILD := $(ROOT)/build/libc
CFLAGS := -m32 -std=gnu11 -O2 -ffreestanding -fno-builtin \
          -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
          -mno-sse -mno-sse2 -Wall -Wextra \
          -I$(ROOT)/kernel/include/uapi -I$(ROOT)/libraries/libc/include

.PHONY: all
all: $(BUILD)/libtinos.a $(BUILD)/crt0.o

$(BUILD)/syscall.o: arch/x86/syscall.c include/tinos/libc.h \
                     $(ROOT)/kernel/include/uapi/tinos/syscall.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/crt0.o: arch/x86/crt0.S $(ROOT)/kernel/include/uapi/tinos/syscall.h
	@mkdir -p $(BUILD)
	$(CC) -m32 -I$(ROOT)/kernel/include/uapi -c $< -o $@

$(BUILD)/ui.o: ui.c include/tinos/ui.h include/tinos/libc.h \
               include/tinos/string.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/widget_font.o: $(ROOT)/kernel/gfx/font8x16.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/widgets.o: widgets.c include/tinos/widgets.h include/tinos/libc.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ui_icons.c: $(ROOT)/tools/gen_user_icons.py \
                     $(ROOT)/tools/gen_icons.py \
                     $(wildcard $(ROOT)/resources/icons/*.bmp)
	@mkdir -p $(BUILD)
	python3 $(ROOT)/tools/gen_user_icons.py $(ROOT)/resources/icons $@

$(BUILD)/ui_icons.o: $(BUILD)/ui_icons.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/widgets_icons.o: widgets_icons.c include/tinos/widgets.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/string.o: string.c include/tinos/string.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/libtinos.a: $(BUILD)/syscall.o $(BUILD)/ui.o $(BUILD)/string.o \
                     $(BUILD)/widgets.o $(BUILD)/widget_font.o \
                     $(BUILD)/widgets_icons.o $(BUILD)/ui_icons.o
	$(AR) rcsD $@ $^
