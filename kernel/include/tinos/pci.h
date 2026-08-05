/* Minimal PCI configuration space access (mechanism #1). */
#ifndef TINOS_PCI_H
#define TINOS_PCI_H

#include <tinos/types.h>

typedef struct {
    uint8_t  bus, slot, func;
    uint16_t vendor_id, device_id;
    uint8_t  class_code, subclass, prog_if, revision;
    uint16_t command, status;
    uint8_t  header_type, irq_line, irq_pin;
} pci_device_t;

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
uint8_t  pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
void     pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func,
                            uint8_t off, uint32_t value);
void     pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func,
                            uint8_t off, uint16_t value);

/* Read base address register `bar` (0-5) with the flag bits masked off. */
uint32_t pci_bar(const pci_device_t *dev, int bar);
bool     pci_enable_io(const pci_device_t *dev);
bool     pci_enable_memory(const pci_device_t *dev);
bool     pci_enable_bus_master(const pci_device_t *dev);

/* Find the nth device matching a class/subclass; returns false if absent. */
bool pci_find_class(uint8_t class_code, uint8_t subclass, int index,
                    pci_device_t *out);

/* Enumerate every device, logging what was found (called during boot). */
size_t pci_scan(void);

#endif /* TINOS_PCI_H */
