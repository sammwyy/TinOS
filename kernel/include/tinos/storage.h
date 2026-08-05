/* Filesystem mounting over discovered block devices. */
#ifndef TINOS_STORAGE_H
#define TINOS_STORAGE_H

#include <tinos/block.h>

/* Mount a raw volume or its primary MBR partitions. */
size_t storage_mount_device(block_device_t *device, size_t *mount_index);

#endif /* TINOS_STORAGE_H */
