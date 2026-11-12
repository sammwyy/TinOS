#include <tinos/bmp.h>

#define BMP_MAX_WIDTH 320u
#define BMP_MAX_HEIGHT 192u

static uint16_t le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int exact(tinos_bmp_reader_t read, void *context,
                 uint8_t *bytes, unsigned length)
{
    while (length) {
        unsigned count = length > 1024 ? 1024 : length;
        int got = read(context, bytes, count);
        if (got <= 0 || (unsigned)got > count)
            return -1;
        bytes += got;
        length -= (unsigned)got;
    }
    return 0;
}

int tinos_bmp_decode(tinos_bmp_reader_t read, void *context,
                     uint32_t *pixels, unsigned capacity,
                     unsigned *width, unsigned *height)
{
    if (!read || !pixels || !width || !height)
        return -1;
    uint8_t header[54];
    if (exact(read, context, header, sizeof(header)) < 0)
        return -1;
    unsigned w = le32(header + 18);
    unsigned raw_h = le32(header + 22);
    unsigned h = (raw_h & 0x80000000u) ? 0u - raw_h : raw_h;
    unsigned bpp = le16(header + 28);
    unsigned offset = le32(header + 10);
    if (header[0] != 'B' || header[1] != 'M' ||
        le32(header + 14) < 40 || le16(header + 26) != 1 ||
        le32(header + 30) != 0 || !w || w > BMP_MAX_WIDTH ||
        !h || h > BMP_MAX_HEIGHT || capacity < w * h ||
        (bpp != 24 && bpp != 32) || offset < sizeof(header) || offset > 4096)
        return -1;

    uint8_t row[4 * BMP_MAX_WIDTH];
    unsigned skip = offset - sizeof(header);
    while (skip) {
        unsigned count = skip > sizeof(row) ? sizeof(row) : skip;
        if (exact(read, context, row, count) < 0)
            return -1;
        skip -= count;
    }
    unsigned stride = (w * (bpp / 8) + 3) & ~3u;
    for (unsigned y = 0; y < h; y++) {
        if (exact(read, context, row, stride) < 0)
            return -1;
        unsigned dst = (raw_h & 0x80000000u) ? y : h - 1 - y;
        for (unsigned x = 0; x < w; x++) {
            const uint8_t *pixel = row + x * (bpp / 8);
            pixels[dst * w + x] = 0xff000000u | ((uint32_t)pixel[2] << 16) |
                                  ((uint32_t)pixel[1] << 8) | pixel[0];
        }
    }
    *width = w;
    *height = h;
    return 0;
}
