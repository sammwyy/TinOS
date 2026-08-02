/* Bochs/QEMU DISPI VGA mode setting. */
#ifndef TINOS_VBE_H
#define TINOS_VBE_H

#include <tinos/types.h>

typedef struct {
    uint16_t dispi_id;
    uint32_t lfb_phys;
    uint16_t vendor_id;
    uint16_t device_id;
} vbe_mode_t;

bool vbe_probe(vbe_mode_t *out);
bool vbe_set_mode(uint16_t width, uint16_t height, uint16_t bpp);
void vbe_disable(void);

#endif /* TINOS_VBE_H */
