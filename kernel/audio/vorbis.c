#include <tinos/codec.h>
#include <tinos/heap.h>
#include <tinos/string.h>

#include "../../third_party/stb_vorbis/stb_vorbis.c"

typedef struct {
    audio_decoder_t base;
    stb_vorbis *vorbis;
} vorbis_decoder_t;

static bool vorbis_matches(const char *path, const uint8_t *data, size_t size)
{
    (void)path;
    return size >= 4 && memcmp(data, "OggS", 4) == 0;
}

static audio_decoder_t *vorbis_open(const uint8_t *data, size_t size,
                                    audio_format_t *format,
                                    uint64_t *total_frames)
{
    int error = 0;
    stb_vorbis *vorbis = stb_vorbis_open_memory(data, (int)size, &error, NULL);
    if (!vorbis) return NULL;
    stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    if (!info.sample_rate || info.channels < 1) {
        stb_vorbis_close(vorbis);
        return NULL;
    }
    vorbis_decoder_t *decoder = kcalloc(1, sizeof(*decoder));
    if (!decoder) { stb_vorbis_close(vorbis); return NULL; }
    decoder->vorbis = vorbis;
    format->sample_rate = info.sample_rate;
    format->channels = 2;
    format->bits_per_sample = 16;
    *total_frames = stb_vorbis_stream_length_in_samples(vorbis);
    return (audio_decoder_t *)decoder;
}

static size_t vorbis_read(audio_decoder_t *decoder, int16_t *out, size_t frames)
{
    vorbis_decoder_t *v = (vorbis_decoder_t *)decoder;
    if (frames > 4096) frames = 4096;
    return (size_t)stb_vorbis_get_samples_short_interleaved(v->vorbis, 2,
                                                            out, (int)frames * 2);
}
static void vorbis_close(audio_decoder_t *decoder)
{
    vorbis_decoder_t *v = (vorbis_decoder_t *)decoder;
    stb_vorbis_close(v->vorbis);
    kfree(v);
}

static audio_codec_t vorbis_codec = {
    .name = "vorbis", .matches = vorbis_matches, .open = vorbis_open,
    .read = vorbis_read, .close = vorbis_close,
};

void vorbis_codec_register(void) { audio_codec_register(&vorbis_codec); }
