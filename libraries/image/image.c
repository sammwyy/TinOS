#include <tinos/image.h>
#include <tinos/heap.h>
#include <tinos/string.h>

static image_codec_t *codecs;
static bool codecs_initialized;

extern void image_png_register(void) __attribute__((weak));
extern void image_jpeg_register(void) __attribute__((weak));
extern void image_webp_register(void) __attribute__((weak));

static void image_init_codecs(void)
{
    if (codecs_initialized)
        return;
    codecs_initialized = true;
    if (image_png_register)
        image_png_register();
    if (image_jpeg_register)
        image_jpeg_register();
    if (image_webp_register)
        image_webp_register();
}

void image_codec_register(image_codec_t *codec)
{
    if (!codec || !codec->matches || !codec->decode)
        return;
    codec->next = codecs;
    codecs = codec;
}

image_result_t image_decode(const uint8_t *data, size_t size,
                            const char *extension, image_t *out)
{
    if (!data || !size || !out)
        return IMAGE_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    image_init_codecs();

    for (image_codec_t *codec = codecs; codec; codec = codec->next) {
        if (!codec->matches(data, size, extension))
            continue;
        return codec->decode(data, size, out);
    }
    return IMAGE_ERR_UNSUPPORTED;
}

void image_destroy(image_t *image)
{
    if (!image)
        return;
    kfree(image->pixels);
    memset(image, 0, sizeof(*image));
}

const char *image_result_string(image_result_t result)
{
    switch (result) {
    case IMAGE_OK:              return "success";
    case IMAGE_ERR_INVALID:     return "invalid image";
    case IMAGE_ERR_UNSUPPORTED: return "unsupported image format";
    case IMAGE_ERR_NOMEM:       return "not enough memory";
    default:                    return "image decode error";
    }
}
