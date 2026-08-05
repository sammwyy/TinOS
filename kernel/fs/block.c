/* Generic validation wrappers for sector-addressable block devices. */

#include <tinos/block.h>

bool block_read(block_device_t *device, uint64_t lba, void *buffer,
                uint32_t sectors)
{
    return device && device->read && device->sector_size == BLOCK_SECTOR_SIZE &&
           device->read(device, lba, buffer, sectors);
}

bool block_write(block_device_t *device, uint64_t lba, const void *buffer,
                 uint32_t sectors)
{
    return device && !device->read_only && device->write &&
           device->sector_size == BLOCK_SECTOR_SIZE &&
           device->write(device, lba, buffer, sectors);
}
