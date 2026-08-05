/* Polling Intel PRO/1000 driver for QEMU's 82540EM device. */

#include <tinos/arch.h>
#include <tinos/dma.h>
#include <tinos/driver.h>
#include <tinos/e1000.h>
#include <tinos/heap.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/pci.h>
#include <tinos/string.h>

#define E1000_VENDOR 0x8086
#define E1000_DEVICE 0x100e
#define RX_COUNT 32
#define TX_COUNT 32
#define BUFFER_SIZE 2048

#define REG_CTRL  0x0000
#define REG_STATUS 0x0008
#define REG_EERD  0x0014
#define REG_IMC   0x00D8
#define REG_RCTL  0x0100
#define REG_TCTL  0x0400
#define REG_TIPG  0x0410
#define REG_RAL   0x540
#define REG_RAH   0x544
#define REG_RDBAL 0x2800
#define REG_RDBAH 0x2804
#define REG_RDLEN 0x2808
#define REG_RDH   0x2810
#define REG_RDT   0x2818
#define REG_TDBAL 0x3800
#define REG_TDBAH 0x3804
#define REG_TDLEN 0x3808
#define REG_TDH   0x3810
#define REG_TDT   0x3818

#define CTRL_RST  0x04000000u
#define CTRL_SLU  0x00000040u
#define RCTL_EN   0x00000002u
#define RCTL_SBP  0x00000004u
#define RCTL_UPE  0x00000008u
#define RCTL_MPE  0x00000010u
#define RCTL_BAM  0x00008000u
#define RCTL_BSIZE_2048 0
#define RCTL_SECRC 0x04000000u
#define TCTL_EN   0x00000002u
#define TCTL_PSP  0x00000008u
#define TX_CMD_EOP 0x01
#define TX_CMD_IFCS 0x02
#define TX_CMD_RS 0x08
#define TX_STATUS_DD 0x01

typedef struct PACKED {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} e1000_rx_desc_t;

typedef struct PACKED {
    uint64_t addr;
    uint16_t length;
    uint8_t checksum;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} e1000_tx_desc_t;

typedef struct {
    pci_device_t pci;
    volatile uint8_t *mmio;
    dma_buffer_t rx_desc_dma, tx_desc_dma;
    dma_buffer_t rx_buffers[RX_COUNT];
    dma_buffer_t tx_buffers[TX_COUNT];
    e1000_rx_desc_t *rx_desc;
    e1000_tx_desc_t *tx_desc;
    uint16_t rx_next, tx_next;
    uint8_t mac[6];
    bool ready;
} e1000_state_t;

static e1000_state_t nic;

static inline uint32_t reg_read(uint32_t reg)
{
    return *(volatile uint32_t *)(nic.mmio + reg);
}

static inline void reg_write(uint32_t reg, uint32_t value)
{
    *(volatile uint32_t *)(nic.mmio + reg) = value;
}

static uint16_t eeprom_read(uint8_t address)
{
    reg_write(REG_EERD, 1u | ((uint32_t)address << 8));
    for (volatile uint32_t i = 0; i < 100000; i++) {
        uint32_t value = reg_read(REG_EERD);
        if (value & (1u << 4))
            return (uint16_t)(value >> 16);
    }
    return 0;
}

static bool e1000_match(const kernel_device_t *device)
{
    if (!device || device->bus != DEVICE_BUS_PCI)
        return false;
    const pci_device_t *pci = (const pci_device_t *)device->bus_data;
    return pci && pci->vendor_id == E1000_VENDOR &&
           pci->device_id == E1000_DEVICE;
}

static void e1000_irq(regs_t *regs)
{
    UNUSED(regs);
    if (nic.ready)
        (void)reg_read(0x00C0); /* ICR: acknowledge pending causes. */
}

static bool e1000_probe(kernel_device_t *device)
{
    const pci_device_t *pci = (const pci_device_t *)device->bus_data;
    memset(&nic, 0, sizeof(nic));
    nic.pci = *pci;
    uint32_t bar = pci_bar(pci, 0);
    if (!bar || (pci_config_read32(pci->bus, pci->slot, pci->func, 0x10) & 1)) {
        klog("[e1000] no MMIO BAR0 (0x%08x)\n", bar);
        return false;
    }
    if (!pci_enable_memory(pci) || !pci_enable_bus_master(pci)) {
        klog("[e1000] PCI command setup failed\n");
        return false;
    }
    nic.mmio = (volatile uint8_t *)(uintptr_t)bar;

    reg_write(REG_CTRL, reg_read(REG_CTRL) | CTRL_RST);
    for (volatile uint32_t i = 0; i < 100000; i++)
        if (!(reg_read(REG_CTRL) & CTRL_RST))
            break;
    reg_write(REG_IMC, 0xFFFFFFFFu);
    reg_write(REG_CTRL, reg_read(REG_CTRL) | CTRL_SLU);

    uint32_t ral = reg_read(REG_RAL);
    uint32_t rah = reg_read(REG_RAH);
    nic.mac[0] = (uint8_t)ral;
    nic.mac[1] = (uint8_t)(ral >> 8);
    nic.mac[2] = (uint8_t)(ral >> 16);
    nic.mac[3] = (uint8_t)(ral >> 24);
    nic.mac[4] = (uint8_t)rah;
    nic.mac[5] = (uint8_t)(rah >> 8);
    if ((rah & 0x80000000u) == 0 || (ral == 0 && rah == 0)) {
        for (int i = 0; i < 3; i++) {
            uint16_t word = eeprom_read((uint8_t)i);
            nic.mac[i * 2] = (uint8_t)word;
            nic.mac[i * 2 + 1] = (uint8_t)(word >> 8);
        }
        if ((nic.mac[0] | nic.mac[1] | nic.mac[2] | nic.mac[3] |
             nic.mac[4] | nic.mac[5]) == 0) {
            klog("[e1000] no MAC in registers or EEPROM\n");
            return false;
        }
    }

    /* Reset clears the receive address table; restore the EEPROM MAC. */
    reg_write(REG_RAL, (uint32_t)nic.mac[0] |
                       ((uint32_t)nic.mac[1] << 8) |
                       ((uint32_t)nic.mac[2] << 16) |
                       ((uint32_t)nic.mac[3] << 24));
    reg_write(REG_RAH, (uint32_t)nic.mac[4] |
                       ((uint32_t)nic.mac[5] << 8) | 0x80000000u);

    if (!dma_alloc(&nic.rx_desc_dma, sizeof(e1000_rx_desc_t) * RX_COUNT) ||
        !dma_alloc(&nic.tx_desc_dma, sizeof(e1000_tx_desc_t) * TX_COUNT)) {
        klog("[e1000] descriptor DMA allocation failed\n");
        return false;
    }
    nic.rx_desc = (e1000_rx_desc_t *)nic.rx_desc_dma.virt;
    nic.tx_desc = (e1000_tx_desc_t *)nic.tx_desc_dma.virt;
    dma_zero(&nic.rx_desc_dma);
    dma_zero(&nic.tx_desc_dma);

    for (int i = 0; i < RX_COUNT; i++) {
        if (!dma_alloc(&nic.rx_buffers[i], BUFFER_SIZE)) {
            klog("[e1000] RX buffer DMA allocation failed at %u\n", i);
            return false;
        }
        nic.rx_desc[i].addr = nic.rx_buffers[i].phys;
        nic.rx_desc[i].status = 0;
    }
    for (int i = 0; i < TX_COUNT; i++) {
        if (!dma_alloc(&nic.tx_buffers[i], BUFFER_SIZE)) {
            klog("[e1000] TX buffer DMA allocation failed at %u\n", i);
            return false;
        }
        nic.tx_desc[i].addr = nic.tx_buffers[i].phys;
        nic.tx_desc[i].status = TX_STATUS_DD;
    }

    reg_write(REG_RDBAL, (uint32_t)nic.rx_desc_dma.phys);
    reg_write(REG_RDBAH, 0);
    reg_write(REG_RDLEN, sizeof(e1000_rx_desc_t) * RX_COUNT);
    reg_write(REG_RDH, 0);
    reg_write(REG_RDT, RX_COUNT - 1);
    reg_write(REG_TDBAL, (uint32_t)nic.tx_desc_dma.phys);
    reg_write(REG_TDBAH, 0);
    reg_write(REG_TDLEN, sizeof(e1000_tx_desc_t) * TX_COUNT);
    reg_write(REG_TDH, 0);
    reg_write(REG_TDT, 0);
    reg_write(REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC |
                         RCTL_BSIZE_2048);
    reg_write(REG_TCTL, TCTL_EN | TCTL_PSP | (0x10u << 4) |
                         (0x40u << 12) | 0x01000000u);
    reg_write(REG_TIPG, 10 | (8u << 10) | (6u << 20));
    nic.rx_next = 0;
    nic.tx_next = 0;
    nic.ready = true;
    if (pci->irq_line < 16)
        irq_install(pci->irq_line, e1000_irq);
    klog("[e1000] %02x:%02x:%02x:%02x:%02x:%02x ready\n",
         nic.mac[0], nic.mac[1], nic.mac[2], nic.mac[3], nic.mac[4], nic.mac[5]);
    return true;
}

static void e1000_remove(kernel_device_t *device)
{
    UNUSED(device);
    nic.ready = false;
}

static kernel_driver_t e1000_driver = {
    .name = "e1000",
    .bus = DEVICE_BUS_PCI,
    .match = e1000_match,
    .probe = e1000_probe,
    .remove = e1000_remove,
};

bool e1000_driver_register(void)
{
    return driver_register(&e1000_driver);
}

bool e1000_available(void) { return nic.ready; }

const uint8_t *e1000_mac(void) { return nic.mac; }

bool e1000_send(const void *data, size_t length)
{
    if (!nic.ready || !data || length == 0 || length > BUFFER_SIZE)
        return false;
    uint16_t index = nic.tx_next;
    e1000_tx_desc_t *desc = &nic.tx_desc[index];
    memcpy(nic.tx_buffers[index].virt, data, length);
    desc->length = (uint16_t)length;
    desc->command = TX_CMD_EOP | TX_CMD_IFCS | TX_CMD_RS;
    desc->status = 0;
    nic.tx_next = (uint16_t)((index + 1) % TX_COUNT);
    reg_write(REG_TDT, nic.tx_next);
    /* Some QEMU e1000 revisions kick the queue on a TCTL write as well. */
    reg_write(REG_TCTL, reg_read(REG_TCTL));
    return true;
}

size_t e1000_receive(void *data, size_t capacity)
{
    if (!nic.ready || !data || capacity == 0)
        return 0;
    e1000_rx_desc_t *desc = &nic.rx_desc[nic.rx_next];
    uint8_t status = *(volatile uint8_t *)&desc->status;
    if (!(status & 1))
        return 0;
    size_t length = MIN((size_t)desc->length, capacity);
    memcpy(data, nic.rx_buffers[nic.rx_next].virt, length);
    desc->status = 0;
    uint16_t old = nic.rx_next;
    nic.rx_next = (uint16_t)((old + 1) % RX_COUNT);
    reg_write(REG_RDT, old);
    return length;
}

void e1000_poll(void)
{
    if (nic.ready)
        (void)reg_read(0x00C0);
}
