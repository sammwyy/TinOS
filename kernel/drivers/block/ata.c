/*
 * Polling ATA PIO driver.
 *
 * This deliberately starts with the small, well-defined legacy IDE path used
 * by QEMU's `-drive if=ide` devices. It supports ATA disks with 28-bit LBA,
 * 512-byte sectors, and no DMA or interrupts. The block layer above it keeps
 * FAT and future filesystems independent from the transport.
 */

#include <tinos/ata.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/printf.h>
#include <tinos/string.h>

#define ATA_PRIMARY_IO       0x1F0
#define ATA_PRIMARY_CONTROL  0x3F6
#define ATA_SECONDARY_IO     0x170
#define ATA_SECONDARY_CONTROL 0x376

#define ATA_REG_DATA         0
#define ATA_REG_ERROR        1
#define ATA_REG_FEATURES     1
#define ATA_REG_SECTOR_COUNT 2
#define ATA_REG_LBA0         3
#define ATA_REG_LBA1         4
#define ATA_REG_LBA2         5
#define ATA_REG_DRIVE        6
#define ATA_REG_STATUS       7
#define ATA_REG_COMMAND      7

#define ATA_CMD_READ_PIO     0x20
#define ATA_CMD_WRITE_PIO    0x30
#define ATA_CMD_CACHE_FLUSH  0xE7
#define ATA_CMD_IDENTIFY     0xEC

#define ATA_STATUS_ERR       0x01
#define ATA_STATUS_DRQ       0x08
#define ATA_STATUS_SRV       0x10
#define ATA_STATUS_DF        0x20
#define ATA_STATUS_RDY       0x40
#define ATA_STATUS_BSY       0x80

#define ATA_TIMEOUT          1000000u

typedef struct {
    uint16_t io;
    uint16_t control;
    uint8_t  slave;
} ata_position_t;

typedef struct {
    ata_position_t position;
    uint32_t       sectors;
    uint16_t       identify[256];
} ata_disk_t;

static const ata_position_t positions[ATA_MAX_DEVICES] = {
    { ATA_PRIMARY_IO,   ATA_PRIMARY_CONTROL,   0 },
    { ATA_PRIMARY_IO,   ATA_PRIMARY_CONTROL,   1 },
    { ATA_SECONDARY_IO, ATA_SECONDARY_CONTROL, 0 },
    { ATA_SECONDARY_IO, ATA_SECONDARY_CONTROL, 1 },
};

static ata_disk_t      disks[ATA_MAX_DEVICES];
static block_device_t  devices[ATA_MAX_DEVICES];
static size_t           device_count;

static uint8_t ata_status(const ata_disk_t *disk)
{
    return inb((uint16_t)(disk->position.io + ATA_REG_STATUS));
}

static bool ata_wait(const ata_disk_t *disk, bool data_request)
{
    for (uint32_t i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t status = ata_status(disk);
        if (status & ATA_STATUS_ERR)
            return false;
        if (status & ATA_STATUS_DF)
            return false;
        if (!(status & ATA_STATUS_BSY) &&
            (!data_request || (status & ATA_STATUS_DRQ)))
            return true;
    }
    return false;
}

static void ata_select(const ata_disk_t *disk, uint32_t lba)
{
    outb((uint16_t)(disk->position.io + ATA_REG_DRIVE),
         (uint8_t)(0xE0 | (disk->position.slave << 4) | ((lba >> 24) & 0x0F)));
    io_wait();
}

static bool ata_identify(ata_disk_t *disk)
{
    uint16_t io = disk->position.io;
    outb((uint16_t)(io + ATA_REG_DRIVE),
         (uint8_t)(0xA0 | (disk->position.slave << 4)));
    io_wait();
    outb((uint16_t)(io + ATA_REG_SECTOR_COUNT), 0);
    outb((uint16_t)(io + ATA_REG_LBA0), 0);
    outb((uint16_t)(io + ATA_REG_LBA1), 0);
    outb((uint16_t)(io + ATA_REG_LBA2), 0);
    outb((uint16_t)(io + ATA_REG_COMMAND), ATA_CMD_IDENTIFY);

    uint8_t status = inb((uint16_t)(io + ATA_REG_STATUS));
    if (status == 0)
        return false;
    if (!ata_wait(disk, true))
        return false;

    for (size_t i = 0; i < ARRAY_SIZE(disk->identify); i++)
        disk->identify[i] = inw(io + ATA_REG_DATA);

    /* QEMU and real ATA disks expose LBA28 support in word 49. */
    if (!(disk->identify[49] & (1u << 9)))
        return false;
    disk->sectors = ((uint32_t)disk->identify[61] << 16) |
                    disk->identify[60];
    return disk->sectors != 0;
}

static bool ata_bounds(const ata_disk_t *disk, uint64_t lba, uint32_t sectors)
{
    return lba <= UINT32_MAX && sectors != 0 &&
           lba + sectors <= disk->sectors && lba + sectors > lba;
}

static bool ata_read(block_device_t *device, uint64_t lba, void *buffer,
                     uint32_t sectors)
{
    ata_disk_t *disk = device ? device->private_data : NULL;
    if (!disk || !buffer || !ata_bounds(disk, lba, sectors))
        return false;

    uint8_t *out = buffer;
    while (sectors--) {
        uint32_t sector = (uint32_t)lba++;
        ata_select(disk, sector);
        uint16_t io = disk->position.io;
        outb((uint16_t)(io + ATA_REG_FEATURES), 0);
        outb((uint16_t)(io + ATA_REG_SECTOR_COUNT), 1);
        outb((uint16_t)(io + ATA_REG_LBA0), (uint8_t)sector);
        outb((uint16_t)(io + ATA_REG_LBA1), (uint8_t)(sector >> 8));
        outb((uint16_t)(io + ATA_REG_LBA2), (uint8_t)(sector >> 16));
        outb((uint16_t)(io + ATA_REG_COMMAND), ATA_CMD_READ_PIO);
        if (!ata_wait(disk, true))
            return false;
        for (size_t i = 0; i < BLOCK_SECTOR_SIZE / 2; i++)
            ((uint16_t *)out)[i] = inw(io + ATA_REG_DATA);
        out += BLOCK_SECTOR_SIZE;
    }
    return true;
}

static bool ata_write(block_device_t *device, uint64_t lba, const void *buffer,
                      uint32_t sectors)
{
    ata_disk_t *disk = device ? device->private_data : NULL;
    if (!disk || !buffer || !ata_bounds(disk, lba, sectors))
        return false;

    const uint8_t *in = buffer;
    while (sectors--) {
        uint32_t sector = (uint32_t)lba++;
        ata_select(disk, sector);
        uint16_t io = disk->position.io;
        outb((uint16_t)(io + ATA_REG_FEATURES), 0);
        outb((uint16_t)(io + ATA_REG_SECTOR_COUNT), 1);
        outb((uint16_t)(io + ATA_REG_LBA0), (uint8_t)sector);
        outb((uint16_t)(io + ATA_REG_LBA1), (uint8_t)(sector >> 8));
        outb((uint16_t)(io + ATA_REG_LBA2), (uint8_t)(sector >> 16));
        outb((uint16_t)(io + ATA_REG_COMMAND), ATA_CMD_WRITE_PIO);
        if (!ata_wait(disk, true))
            return false;
        for (size_t i = 0; i < BLOCK_SECTOR_SIZE / 2; i++)
            outw(io + ATA_REG_DATA, ((const uint16_t *)in)[i]);
        outb((uint16_t)(io + ATA_REG_COMMAND), ATA_CMD_CACHE_FLUSH);
        if (!ata_wait(disk, false))
            return false;
        in += BLOCK_SECTOR_SIZE;
    }
    return true;
}

size_t ata_pio_init(void)
{
    device_count = 0;
    memset(disks, 0, sizeof(disks));
    memset(devices, 0, sizeof(devices));

    for (size_t i = 0; i < ARRAY_SIZE(positions); i++) {
        ata_disk_t *disk = &disks[i];
        disk->position = positions[i];
        if (!ata_identify(disk))
            continue;

        block_device_t *device = &devices[device_count];
        ksnprintf(device->name, sizeof(device->name), "ata%u", (uint32_t)i);
        device->sector_count = disk->sectors;
        device->sector_size = BLOCK_SECTOR_SIZE;
        device->read_only = false;
        device->private_data = disk;
        device->read = ata_read;
        device->write = ata_write;
        klog("[ata ] %s: %u sectors (%u MiB)\n", device->name,
             disk->sectors, disk->sectors / 2048);
        device_count++;
    }
    return device_count;
}

size_t ata_pio_count(void)
{
    return device_count;
}

block_device_t *ata_pio_device(size_t index)
{
    return index < device_count ? &devices[index] : NULL;
}
