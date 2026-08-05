/* Primary MBR partition views. Extended partitions are intentionally left
 * for a later partition-table pass; the block-device boundary is already
 * sufficient for FAT32 and other filesystems. */

#include <tinos/mbr.h>
#include <tinos/printf.h>
#include <tinos/string.h>

typedef struct {
    block_device_t *parent;
    uint32_t        start;
    uint32_t        sectors;
} partition_context_t;

static partition_context_t contexts[MBR_MAX_PARTITIONS];

static bool partition_read(block_device_t *device, uint64_t lba,
                           void *buffer, uint32_t sectors)
{
    partition_context_t *context = device ? device->private_data : NULL;
    if (!context || lba + sectors > context->sectors || lba + sectors < lba)
        return false;
    return block_read(context->parent, context->start + lba, buffer, sectors);
}

static bool partition_write(block_device_t *device, uint64_t lba,
                            const void *buffer, uint32_t sectors)
{
    partition_context_t *context = device ? device->private_data : NULL;
    if (!context || lba + sectors > context->sectors || lba + sectors < lba)
        return false;
    return block_write(context->parent, context->start + lba, buffer, sectors);
}

size_t mbr_partitions(block_device_t *disk, block_device_t *out, size_t capacity)
{
    if (!disk || !out || capacity == 0)
        return 0;

    uint8_t sector[BLOCK_SECTOR_SIZE];
    if (!block_read(disk, 0, sector, 1) ||
        sector[510] != 0x55 || sector[511] != 0xAA)
        return 0;

    size_t count = 0;
    size_t limit = capacity < MBR_MAX_PARTITIONS ? capacity : MBR_MAX_PARTITIONS;
    for (size_t i = 0; i < limit; i++) {
        const uint8_t *entry = sector + 446 + i * 16;
        uint8_t type = entry[4];
        uint32_t start = (uint32_t)entry[8] | ((uint32_t)entry[9] << 8) |
                         ((uint32_t)entry[10] << 16) |
                         ((uint32_t)entry[11] << 24);
        uint32_t sectors = (uint32_t)entry[12] | ((uint32_t)entry[13] << 8) |
                           ((uint32_t)entry[14] << 16) |
                           ((uint32_t)entry[15] << 24);
        if (type == 0 || sectors == 0 || start >= disk->sector_count ||
            sectors > disk->sector_count - start)
            continue;

        partition_context_t *context = &contexts[count];
        context->parent = disk;
        context->start = start;
        context->sectors = sectors;
        block_device_t *partition = &out[count];
        ksnprintf(partition->name, sizeof(partition->name), "%sp%u",
                  disk->name, (uint32_t)(i + 1));
        partition->sector_count = sectors;
        partition->sector_size = disk->sector_size;
        partition->read_only = disk->read_only;
        partition->private_data = context;
        partition->read = partition_read;
        partition->write = partition_write;
        count++;
    }
    return count;
}
