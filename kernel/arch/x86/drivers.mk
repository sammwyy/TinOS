# Drivers built for ARCH=x86, relative to kernel/drivers/.
#
# Every architecture lists the drivers it can use in its own
# kernel/arch/<ARCH>/drivers.mk. A driver that depends on x86 port I/O or on
# PC-only hardware (i8042, VGA text, PC speaker, ISA ATA) is simply left out
# of another architecture's list.
ARCH_DRIVERS := \
    audio/audio.c \
    audio/speaker.c \
    block/ata.c \
    block/virtio_blk.c \
    bus/pci.c \
    char/serial.c \
    input/keyboard.c \
    input/mouse.c \
    net/e1000.c \
    usb/uhci.c \
    usb/usb_msc.c \
    video/vbe.c \
    video/vga_text.c
