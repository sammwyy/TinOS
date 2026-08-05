/* Read-only FAT16 importer for block devices. */
#ifndef TINOS_FAT16_H
#define TINOS_FAT16_H

#include <tinos/block.h>
#include <tinos/fs.h>

/* Import the FAT16 volume into `mountpoint`. Existing children are kept. */
bool fat16_import(block_device_t *device, fs_node_t *mountpoint);

#endif /* TINOS_FAT16_H */
