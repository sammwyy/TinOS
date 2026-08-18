#ifndef TINOS_IMAGE_H
#define TINOS_IMAGE_H

#include <tinos/types.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t *pixels;
} image_t;

typedef enum {
    IMAGE_OK = 0,
    IMAGE_ERR_INVALID,
    IMAGE_ERR_UNSUPPORTED,
    IMAGE_ERR_NOMEM,
} image_result_t;

typedef struct image_codec {
    const char *name;
    bool (*matches)(const uint8_t *data, size_t size, const char *extension);
    image_result_t (*decode)(const uint8_t *data, size_t size, image_t *out);
    struct image_codec *next;
} image_codec_t;

void image_codec_register(image_codec_t *codec);
image_result_t image_decode(const uint8_t *data, size_t size,
                            const char *extension, image_t *out);
void image_destroy(image_t *image);
const char *image_result_string(image_result_t result);

#endif
