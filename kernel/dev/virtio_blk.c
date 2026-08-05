/*
 * Minimal synchronous driver for the transitional PCI VirtIO block device.
 * It uses one legacy virtqueue and 512-byte requests, which is enough to
 * reuse the filesystem stack without introducing a scheduler dependency.
 */

#include <tinos/dma.h>
#include <tinos/driver.h>
#include <tinos/kernel.h>
#include <tinos/io.h>
#include <tinos/pci.h>
#include <tinos/printf.h>
#include <tinos/string.h>
#include <tinos/virtio_blk.h>

#define VIRTIO_VENDOR             0x1AF4
#define VIRTIO_BLK_LEGACY         0x1001
#define VIRTIO_QUEUE_SIZE         8
#define VIRTIO_QUEUE_BYTES        8192
#define VIRTIO_REQUEST_BYTES      1024
#define VIRTIO_TIMEOUT             1000000u

#define VIRTIO_STATUS_ACK          1
#define VIRTIO_STATUS_DRIVER       2
#define VIRTIO_STATUS_DRIVER_OK    4

#define VIRTIO_DESC_F_NEXT         1
#define VIRTIO_DESC_F_WRITE        2

typedef struct PACKED {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
} virtio_desc_t;

typedef struct PACKED {
    uint16_t flags;
    uint16_t index;
    uint16_t ring[VIRTIO_QUEUE_SIZE];
} virtio_avail_t;

typedef struct PACKED {
    uint32_t index;
    uint32_t length;
} virtio_used_elem_t;

typedef struct PACKED {
    uint16_t flags;
    uint16_t index;
    virtio_used_elem_t ring[VIRTIO_QUEUE_SIZE];
} virtio_used_t;

typedef struct PACKED {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} virtio_request_t;

typedef struct {
    uint16_t io;
    pci_device_t pci;
    dma_buffer_t queue_dma;
    dma_buffer_t request_dma;
    virtio_desc_t *descs;
    virtio_avail_t *avail;
    virtio_used_t *used;
    virtio_request_t *request;
    uint8_t *data;
    uint8_t *status;
    uint16_t last_used;
    block_device_t block;
    bool ready;
} virtio_state_t;

static virtio_state_t state;

static bool virtio_match(const kernel_device_t *device)
{
    if (!device || device->bus != DEVICE_BUS_PCI)
        return false;
    const pci_device_t *pci = device->bus_data;
    return pci && pci->vendor_id == VIRTIO_VENDOR &&
           pci->device_id == VIRTIO_BLK_LEGACY;
}

static void virtio_status(uint8_t status)
{
    outb((uint16_t)(state.io + 0x12), status);
}

static bool virtio_wait_used(uint16_t expected)
{
    for (uint32_t i = 0; i < VIRTIO_TIMEOUT; i++) {
        if (state.used->index == expected)
            return true;
    }
    return false;
}

static bool virtio_request(bool write, uint64_t sector, void *buffer)
{
    if (!state.ready || !buffer)
        return false;

    state.request->type = write ? 1 : 0;
    state.request->reserved = 0;
    state.request->sector = sector;
    *state.status = 0xFF;

    state.descs[0] = (virtio_desc_t){ state.request_dma.phys,
                                      sizeof(virtio_request_t),
                                      VIRTIO_DESC_F_NEXT, 1 };
    state.descs[1] = (virtio_desc_t){ state.request_dma.phys + 64,
                                      BLOCK_SECTOR_SIZE,
                                      (uint16_t)(VIRTIO_DESC_F_NEXT |
                                                 (write ? 0 : VIRTIO_DESC_F_WRITE)),
                                      2 };
    state.descs[2] = (virtio_desc_t){ state.request_dma.phys + 576, 1,
                                      VIRTIO_DESC_F_WRITE, 0 };
    if (write)
        memcpy(state.data, buffer, BLOCK_SECTOR_SIZE);

    uint16_t next = state.avail->index;
    state.avail->ring[next % VIRTIO_QUEUE_SIZE] = 0;
    __asm__ volatile("" ::: "memory");
    state.avail->index = (uint16_t)(next + 1);
    outw((uint16_t)(state.io + 0x10), 0);

    uint16_t expected = state.last_used + 1;
    if (!virtio_wait_used(expected) || *state.status != 0)
        return false;
    state.last_used = expected;
    if (!write)
        memcpy(buffer, state.data, BLOCK_SECTOR_SIZE);
    return true;
}

static bool virtio_read(block_device_t *device, uint64_t lba, void *buffer,
                        uint32_t sectors)
{
    if (!device || !buffer || lba + sectors > device->sector_count ||
        lba + sectors < lba)
        return false;
    uint8_t *out = buffer;
    while (sectors--)
        if (!virtio_request(false, lba++, out))
            return false;
        else
            out += BLOCK_SECTOR_SIZE;
    return true;
}

static bool virtio_write(block_device_t *device, uint64_t lba,
                         const void *buffer, uint32_t sectors)
{
    if (!device || !buffer || lba + sectors > device->sector_count ||
        lba + sectors < lba)
        return false;
    const uint8_t *in = buffer;
    while (sectors--)
        if (!virtio_request(true, lba++, (void *)in))
            return false;
        else
            in += BLOCK_SECTOR_SIZE;
    return true;
}

static bool virtio_probe(kernel_device_t *device)
{
    const pci_device_t *pci = device->bus_data;
    memset(&state, 0, sizeof(state));
    state.pci = *pci;
    uint32_t bar = pci_bar(pci, 0);
    if (!bar || !(pci_config_read32(pci->bus, pci->slot, pci->func, 0x10) & 1) ||
        !pci_enable_io(pci))
        return false;
    state.io = (uint16_t)bar;

    virtio_status(0);
    virtio_status(VIRTIO_STATUS_ACK);
    virtio_status(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);
    (void)inl((uint16_t)(state.io + 0x00)); /* feature negotiation: none */
    outl((uint16_t)(state.io + 0x04), 0);
    outw((uint16_t)(state.io + 0x0E), 0);
    uint16_t queue_size = inw((uint16_t)(state.io + 0x0C));
    if (queue_size < 3)
        return false;
    if (queue_size > VIRTIO_QUEUE_SIZE)
        queue_size = VIRTIO_QUEUE_SIZE;
    (void)queue_size;

    uint64_t capacity = (uint64_t)inl((uint16_t)(state.io + 0x14)) |
                        ((uint64_t)inl((uint16_t)(state.io + 0x18)) << 32);
    if (capacity == 0)
        return false;

    if (!dma_alloc(&state.queue_dma, VIRTIO_QUEUE_BYTES) ||
        !dma_alloc(&state.request_dma, VIRTIO_REQUEST_BYTES))
        return false;
    dma_zero(&state.queue_dma);
    dma_zero(&state.request_dma);
    state.descs = state.queue_dma.virt;
    state.avail = (void *)((uintptr_t)state.queue_dma.virt + 128);
    state.used = (void *)((uintptr_t)state.queue_dma.virt + 4096);
    state.request = state.request_dma.virt;
    state.data = (uint8_t *)state.request_dma.virt + 64;
    state.status = (uint8_t *)state.request_dma.virt + 576;

    outw((uint16_t)(state.io + 0x0E), 0);
    outl((uint16_t)(state.io + 0x08), (uint32_t)(state.queue_dma.phys >> 12));
    state.block.sector_count = capacity;
    state.block.sector_size = BLOCK_SECTOR_SIZE;
    state.block.read_only = false;
    state.block.private_data = &state;
    state.block.read = virtio_read;
    state.block.write = virtio_write;
    ksnprintf(state.block.name, sizeof(state.block.name), "virtio0");
    state.ready = true;
    virtio_status(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                  VIRTIO_STATUS_DRIVER_OK);
    klog("[virt] %s ready\n", state.block.name);
    return true;
}

static void virtio_remove(kernel_device_t *device)
{
    UNUSED(device);
    state.ready = false;
    dma_free(&state.queue_dma);
    dma_free(&state.request_dma);
}

static kernel_driver_t driver = {
    .name = "virtio-blk",
    .bus = DEVICE_BUS_PCI,
    .match = virtio_match,
    .probe = virtio_probe,
    .remove = virtio_remove,
};

bool virtio_blk_driver_register(void)
{
    return driver_register(&driver);
}

size_t virtio_blk_count(void)
{
    return state.ready ? 1 : 0;
}

block_device_t *virtio_blk_device(size_t index)
{
    return index == 0 && state.ready ? &state.block : NULL;
}
