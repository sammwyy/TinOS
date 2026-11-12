#ifndef TINOS_USER_BMP_H
#define TINOS_USER_BMP_H

#include <stdint.h>

typedef int (*tinos_bmp_reader_t)(void *context, void *buffer, unsigned count);

/* Returns zero on success. The reader is positioned at the start of a BMP. */
int tinos_bmp_decode(tinos_bmp_reader_t read, void *context,
                     uint32_t *pixels, unsigned capacity,
                     unsigned *width, unsigned *height);

#endif
