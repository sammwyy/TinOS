/* Transitional PCI VirtIO block device. */
#ifndef TINOS_VIRTIO_BLK_H
#define TINOS_VIRTIO_BLK_H

#include <tinos/block.h>

bool virtio_blk_driver_register(void);
size_t virtio_blk_count(void);
block_device_t *virtio_blk_device(size_t index);

#endif /* TINOS_VIRTIO_BLK_H */
