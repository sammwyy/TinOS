/* Read-only FAT32 importer for block devices. */
#ifndef TINOS_FAT32_H
#define TINOS_FAT32_H

#include <tinos/block.h>
#include <tinos/fs.h>

bool fat32_import(block_device_t *device, fs_node_t *mountpoint);

#endif /* TINOS_FAT32_H */
