/* Sector-addressable block devices. */
#ifndef TINOS_BLOCK_H
#define TINOS_BLOCK_H

#include <tinos/types.h>

#define BLOCK_SECTOR_SIZE 512u
#define BLOCK_DEVICE_NAME 32

typedef struct block_device block_device_t;

typedef bool (*block_read_fn)(block_device_t *device, uint64_t lba,
                             void *buffer, uint32_t sectors);
typedef bool (*block_write_fn)(block_device_t *device, uint64_t lba,
                               const void *buffer, uint32_t sectors);

struct block_device {
    char           name[BLOCK_DEVICE_NAME];
    uint64_t       sector_count;
    uint32_t       sector_size;
    bool           read_only;
    void          *private_data;
    block_read_fn  read;
    block_write_fn write;
};

bool block_read(block_device_t *device, uint64_t lba, void *buffer,
                uint32_t sectors);
bool block_write(block_device_t *device, uint64_t lba, const void *buffer,
                 uint32_t sectors);

#endif /* TINOS_BLOCK_H */
