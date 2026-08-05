#include <tinos/image.h>
#include <tinos/heap.h>
#include <tinos/string.h>

typedef struct simplewebp simplewebp_t;
typedef struct simplewebp_allocator {
    void *(*alloc)(void *userdata, size_t size);
    void (*free)(void *userdata, void *mem);
    void *userdata;
} simplewebp_allocator_t;
typedef struct simplewebp_input {
    void *userdata;
    void (*close)(void *userdata);
    size_t (*read)(size_t size, void *dest, void *userdata);
    bool (*seek)(size_t position, void *userdata);
    size_t (*tell)(void *userdata);
} simplewebp_input_t;

typedef enum {
    SIMPLEWEBP_NO_ERROR = 0,
} simplewebp_error_t;

extern simplewebp_error_t simplewebp_input_from_memory(
    void *data, size_t size, simplewebp_input_t *out,
    const simplewebp_allocator_t *allocator);
extern simplewebp_error_t simplewebp_load(
    simplewebp_input_t *input, const simplewebp_allocator_t *allocator,
    simplewebp_t **out);
extern void simplewebp_unload(simplewebp_t *image);
extern void simplewebp_get_dimensions(simplewebp_t *image, size_t *width,
                                       size_t *height);
extern simplewebp_error_t simplewebp_decode(simplewebp_t *image, void *buffer,
                                             void *settings);

static void *webp_alloc(void *userdata, size_t size)
{
    (void)userdata;
    return kmalloc(size);
}

static void webp_free(void *userdata, void *memory)
{
    (void)userdata;
    kfree(memory);
}

static const simplewebp_allocator_t webp_allocator = {
    .alloc = webp_alloc,
    .free = webp_free,
};

static bool webp_matches(const uint8_t *data, size_t size,
                         const char *extension)
{
    bool extension_match = extension && kstrcasecmp(extension, "webp") == 0;
    return (size >= 12 && memcmp(data, "RIFF", 4) == 0 &&
            memcmp(data + 8, "WEBP", 4) == 0) || extension_match;
}

static image_result_t webp_decode(const uint8_t *data, size_t size,
                                  image_t *out)
{
    simplewebp_input_t input;
    simplewebp_t *decoded = NULL;
    size_t width, height;
    image_result_t result = IMAGE_ERR_INVALID;

    if (simplewebp_input_from_memory((void *)data, size, &input,
                                     &webp_allocator) != SIMPLEWEBP_NO_ERROR)
        return IMAGE_ERR_INVALID;
    if (simplewebp_load(&input, &webp_allocator, &decoded) !=
        SIMPLEWEBP_NO_ERROR)
        return IMAGE_ERR_INVALID;

    simplewebp_get_dimensions(decoded, &width, &height);
    if (!width || !height || width > SIZE_MAX / height ||
        width * height > SIZE_MAX / 4)
        goto done;

    size_t count = width * height;
    uint8_t *rgba = kmalloc(count * 4);
    if (!rgba) {
        result = IMAGE_ERR_NOMEM;
        goto done;
    }
    if (simplewebp_decode(decoded, rgba, NULL) != SIMPLEWEBP_NO_ERROR) {
        kfree(rgba);
        goto done;
    }

    out->pixels = kmalloc(count * sizeof(uint32_t));
    if (!out->pixels) {
        kfree(rgba);
        result = IMAGE_ERR_NOMEM;
        goto done;
    }
    for (size_t i = 0; i < count; i++) {
        out->pixels[i] = ((uint32_t)rgba[i * 4 + 3] << 24) |
                         ((uint32_t)rgba[i * 4 + 0] << 16) |
                         ((uint32_t)rgba[i * 4 + 1] << 8) |
                         rgba[i * 4 + 2];
    }
    kfree(rgba);
    out->width = (uint32_t)width;
    out->height = (uint32_t)height;
    result = IMAGE_OK;

done:
    simplewebp_unload(decoded);
    return result;
}

static image_codec_t webp_codec = {
    .name = "webp",
    .matches = webp_matches,
    .decode = webp_decode,
};

void image_webp_register(void)
{
    image_codec_register(&webp_codec);
}
