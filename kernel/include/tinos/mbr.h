/* MBR partition views over an existing block device. */
#ifndef TINOS_MBR_H
#define TINOS_MBR_H

#include <tinos/block.h>

#define MBR_MAX_PARTITIONS 4

/* Populate `out` with primary partition block devices from an MBR disk. */
size_t mbr_partitions(block_device_t *disk, block_device_t *out, size_t capacity);

#endif /* TINOS_MBR_H */
