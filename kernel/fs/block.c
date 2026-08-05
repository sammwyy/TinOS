/* Generic validation wrappers for sector-addressable block devices. */

#include <tinos/block.h>
#ifndef TINOS_HOST
#include <tinos/thread.h>
#endif

bool block_read(block_device_t *device, uint64_t lba, void *buffer,
                uint32_t sectors)
{
    bool ok = device && device->read &&
              device->sector_size == BLOCK_SECTOR_SIZE &&
              device->read(device, lba, buffer, sectors);
#ifndef TINOS_HOST
    thread_yield();
#endif
    return ok;
}

bool block_write(block_device_t *device, uint64_t lba, const void *buffer,
                 uint32_t sectors)
{
    bool ok = device && !device->read_only && device->write &&
              device->sector_size == BLOCK_SECTOR_SIZE &&
              device->write(device, lba, buffer, sectors);
#ifndef TINOS_HOST
    thread_yield();
#endif
    return ok;
}
