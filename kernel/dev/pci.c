/* PCI bus enumeration via the classic 0xCF8/0xCFC configuration mechanism.
 *
 * The graphics driver uses this to locate the display adapter and read the
 * address of its linear framebuffer out of BAR0.
 */

#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/pci.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static uint32_t config_address(uint8_t bus, uint8_t slot, uint8_t func,
                               uint8_t offset)
{
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
           ((uint32_t)func << 8) | (offset & 0xFCu);
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func,
                           uint8_t offset)
{
    outl(PCI_CONFIG_ADDRESS, config_address(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func,
                           uint8_t offset)
{
    uint32_t dword = pci_config_read32(bus, slot, func, offset);
    return (uint16_t)((dword >> ((offset & 2) * 8)) & 0xFFFF);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset,
                        uint32_t value)
{
    outl(PCI_CONFIG_ADDRESS, config_address(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

static bool read_device(uint8_t bus, uint8_t slot, uint8_t func,
                        pci_device_t *out)
{
    uint32_t id = pci_config_read32(bus, slot, func, 0x00);
    uint16_t vendor = (uint16_t)(id & 0xFFFF);
    if (vendor == 0xFFFF)
        return false;

    uint32_t classes = pci_config_read32(bus, slot, func, 0x08);
    out->bus        = bus;
    out->slot       = slot;
    out->func       = func;
    out->vendor_id  = vendor;
    out->device_id  = (uint16_t)(id >> 16);
    out->revision   = (uint8_t)(classes & 0xFF);
    out->prog_if    = (uint8_t)((classes >> 8) & 0xFF);
    out->subclass   = (uint8_t)((classes >> 16) & 0xFF);
    out->class_code = (uint8_t)((classes >> 24) & 0xFF);
    return true;
}

uint32_t pci_bar(const pci_device_t *dev, int bar)
{
    if (bar < 0 || bar > 5)
        return 0;
    uint32_t value =
        pci_config_read32(dev->bus, dev->slot, dev->func,
                          (uint8_t)(0x10 + bar * 4));
    if (value & 1)
        return value & ~0x3u; /* I/O space */
    return value & ~0xFu;     /* memory space */
}

bool pci_find_class(uint8_t class_code, uint8_t subclass, int index,
                    pci_device_t *out)
{
    int matches = 0;
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                pci_device_t dev;
                if (!read_device((uint8_t)bus, (uint8_t)slot, (uint8_t)func,
                                 &dev))
                    continue;
                if (dev.class_code == class_code && dev.subclass == subclass) {
                    if (matches++ == index) {
                        *out = dev;
                        return true;
                    }
                }
                if (func == 0) {
                    uint32_t header =
                        pci_config_read32((uint8_t)bus, (uint8_t)slot, 0, 0x0C);
                    bool multifunction = ((header >> 16) & 0x80) != 0;
                    if (!multifunction)
                        break;
                }
            }
        }
    }
    return false;
}

size_t pci_scan(void)
{
    size_t count = 0;
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                pci_device_t dev;
                if (!read_device((uint8_t)bus, (uint8_t)slot, (uint8_t)func,
                                 &dev))
                    continue;
                count++;
                klog("[pci ] %02x:%02x.%u %04x:%04x class %02x.%02x\n", bus,
                     slot, func, dev.vendor_id, dev.device_id, dev.class_code,
                     dev.subclass);
                if (func == 0) {
                    uint32_t header =
                        pci_config_read32((uint8_t)bus, (uint8_t)slot, 0, 0x0C);
                    if (!((header >> 16) & 0x80))
                        break;
                }
            }
        }
    }
    return count;
}
