/* Legacy ATA/IDE PIO block devices. */
#ifndef TINOS_ATA_H
#define TINOS_ATA_H

#include <tinos/block.h>
#include <tinos/types.h>

#define ATA_MAX_DEVICES 4

/* Probe the four legacy IDE positions and register any ATA disks found. */
size_t ata_pio_init(void);
size_t ata_pio_count(void);
block_device_t *ata_pio_device(size_t index);

#endif /* TINOS_ATA_H */
