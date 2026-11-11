/* VGA graphics mode setting.
 *
 * QEMU's standard VGA adapter (and Bochs, and VirtualBox) implement the
 * "DISPI" register interface: an index/data port pair that programs the
 * resolution and colour depth of a linear framebuffer without needing to
 * return to real mode for a BIOS call.  That makes it exactly what a
 * 32-bit protected mode kernel needs.
 *
 * The framebuffer address itself is read from BAR0 of the PCI display
 * device, so we do not have to hardcode QEMU's 0xFD000000.
 */

#include <tinos/gfx.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/pci.h>
#include <tinos/vbe.h>

#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA  0x01CF

enum {
    VBE_DISPI_INDEX_ID = 0,
    VBE_DISPI_INDEX_XRES,
    VBE_DISPI_INDEX_YRES,
    VBE_DISPI_INDEX_BPP,
    VBE_DISPI_INDEX_ENABLE,
    VBE_DISPI_INDEX_BANK,
    VBE_DISPI_INDEX_VIRT_WIDTH,
    VBE_DISPI_INDEX_VIRT_HEIGHT,
    VBE_DISPI_INDEX_X_OFFSET,
    VBE_DISPI_INDEX_Y_OFFSET,
};

#define VBE_DISPI_DISABLED  0x00
#define VBE_DISPI_ENABLED   0x01
#define VBE_DISPI_LFB       0x40
#define VBE_DISPI_NOCLEAR   0x80

#define VBE_DISPI_ID0 0xB0C0
#define VBE_DISPI_ID5 0xB0C5

static void dispi_write(uint16_t index, uint16_t value)
{
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

static uint16_t dispi_read(uint16_t index)
{
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

bool vbe_probe(vbe_mode_t *out)
{
    uint16_t id = dispi_read(VBE_DISPI_INDEX_ID);
    if (id < VBE_DISPI_ID0 || id > VBE_DISPI_ID5) {
        klog("[vbe ] no Bochs/QEMU DISPI interface (id=0x%04x)\n", id);
        return false;
    }

    pci_device_t dev;
    if (!pci_find_class(0x03, 0x00, 0, &dev)) { /* VGA compatible controller */
        klog("[vbe ] no PCI display adapter found\n");
        return false;
    }

    uint32_t lfb = pci_bar(&dev, 0);
    if (lfb == 0) {
        klog("[vbe ] display adapter has no framebuffer BAR\n");
        return false;
    }

    /* Make sure memory space decoding and bus mastering are enabled. */
    uint32_t command = pci_config_read32(dev.bus, dev.slot, dev.func, 0x04);
    pci_config_write32(dev.bus, dev.slot, dev.func, 0x04, command | 0x07);

    out->dispi_id  = id;
    out->lfb_phys  = lfb;
    out->vendor_id = dev.vendor_id;
    out->device_id = dev.device_id;
    klog("[vbe ] DISPI 0x%04x on %04x:%04x, framebuffer at 0x%08x\n", id,
         dev.vendor_id, dev.device_id, lfb);
    return true;
}

bool vbe_set_mode(uint16_t width, uint16_t height, uint16_t bpp)
{
    dispi_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    dispi_write(VBE_DISPI_INDEX_XRES, width);
    dispi_write(VBE_DISPI_INDEX_YRES, height);
    dispi_write(VBE_DISPI_INDEX_BPP, bpp);
    dispi_write(VBE_DISPI_INDEX_VIRT_WIDTH, width);
    dispi_write(VBE_DISPI_INDEX_VIRT_HEIGHT, height);
    dispi_write(VBE_DISPI_INDEX_X_OFFSET, 0);
    dispi_write(VBE_DISPI_INDEX_Y_OFFSET, 0);
    dispi_write(VBE_DISPI_INDEX_BANK, 0);
    dispi_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB);

    /* Verify the adapter accepted the mode rather than trusting it. */
    uint16_t got_w  = dispi_read(VBE_DISPI_INDEX_XRES);
    uint16_t got_h  = dispi_read(VBE_DISPI_INDEX_YRES);
    uint16_t got_b  = dispi_read(VBE_DISPI_INDEX_BPP);
    uint16_t enable = dispi_read(VBE_DISPI_INDEX_ENABLE);

    if (got_w != width || got_h != height || got_b != bpp ||
        !(enable & VBE_DISPI_ENABLED)) {
        klog("[vbe ] mode %ux%ux%u rejected (got %ux%ux%u enable=0x%04x)\n",
             width, height, bpp, got_w, got_h, got_b, enable);
        return false;
    }
    klog("[vbe ] mode set to %ux%ux%u linear\n", width, height, bpp);
    return true;
}

void vbe_disable(void)
{
    dispi_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
}
