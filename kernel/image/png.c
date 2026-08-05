#include <tinos/image.h>
#include <tinos/heap.h>
#include <tinos/string.h>

extern unsigned char *stbi_load_from_memory(const unsigned char *buffer,
                                             int len, int *x, int *y,
                                             int *channels_in_file,
                                             int desired_channels);
extern void stbi_image_free(void *retval_from_stbi_load);

static bool png_matches(const uint8_t *data, size_t size,
                        const char *extension)
{
    (void)extension;
    static const uint8_t signature[] = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'
    };
    return size >= sizeof(signature) && memcmp(data, signature,
                                                sizeof(signature)) == 0;
}

static image_result_t png_decode(const uint8_t *data, size_t size,
                                 image_t *out)
{
    int width, height, channels;
    unsigned char *rgba = stbi_load_from_memory(data, (int)size, &width,
                                                 &height, &channels, 4);
    if (!rgba)
        return IMAGE_ERR_INVALID;
    if (width <= 0 || height <= 0 ||
        (size_t)width > SIZE_MAX / (size_t)height) {
        stbi_image_free(rgba);
        return IMAGE_ERR_INVALID;
    }

    size_t count = (size_t)width * (size_t)height;
    if (count > SIZE_MAX / sizeof(uint32_t)) {
        stbi_image_free(rgba);
        return IMAGE_ERR_NOMEM;
    }
    out->pixels = kmalloc(count * sizeof(uint32_t));
    if (!out->pixels) {
        stbi_image_free(rgba);
        return IMAGE_ERR_NOMEM;
    }
    for (size_t i = 0; i < count; i++) {
        uint32_t r = rgba[i * 4 + 0];
        uint32_t g = rgba[i * 4 + 1];
        uint32_t b = rgba[i * 4 + 2];
        uint32_t a = rgba[i * 4 + 3];
        out->pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    stbi_image_free(rgba);
    out->width = (uint32_t)width;
    out->height = (uint32_t)height;
    return IMAGE_OK;
}

static image_codec_t png_codec = {
    .name = "png",
    .matches = png_matches,
    .decode = png_decode,
};

void image_png_register(void)
{
    image_codec_register(&png_codec);
}
