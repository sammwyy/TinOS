#include <tinos/codec.h>
#include <tinos/heap.h>
#include <tinos/string.h>

typedef struct {
    audio_decoder_t base;
    const uint8_t *data;
    size_t bytes;
    size_t cursor;
    uint16_t channels;
    uint16_t bits;
    uint32_t rate;
    uint32_t block_align;
} wav_decoder_t;

static uint16_t le16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
static bool tag(const uint8_t *p, const char *s) { return memcmp(p, s, 4) == 0; }

static bool wav_matches(const char *path, const uint8_t *data, size_t size)
{
    (void)path;
    return size >= 12 && tag(data, "RIFF") && tag(data + 8, "WAVE");
}

static audio_decoder_t *wav_open(const uint8_t *data, size_t size,
                                 audio_format_t *format,
                                 uint64_t *total_frames)
{
    if (!wav_matches(NULL, data, size))
        return NULL;
    uint32_t riff_size = le32(data + 4);
    if (riff_size < 4 || riff_size > size - 8)
        return NULL;
    size_t at = 12, fmt_at = 0, fmt_size = 0, pcm_at = 0, pcm_size = 0;
    while (at + 8 <= size) {
        uint32_t chunk = le32(data + at + 4);
        size_t next = at + 8u + chunk + (chunk & 1u);
        if (next < at + 8 || next > size)
            return NULL;
        if (tag(data + at, "fmt ")) { fmt_at = at + 8; fmt_size = chunk; }
        if (tag(data + at, "data")) { pcm_at = at + 8; pcm_size = chunk; }
        at = next;
    }
    if (!fmt_at || !pcm_at || fmt_size < 16 || pcm_size == 0 ||
        fmt_at + fmt_size > size || pcm_at + pcm_size > size)
        return NULL;
    uint16_t format_tag = le16(data + fmt_at);
    uint16_t channels = le16(data + fmt_at + 2);
    uint32_t rate = le32(data + fmt_at + 4);
    uint16_t align = le16(data + fmt_at + 12);
    uint16_t bits = le16(data + fmt_at + 14);
    if (format_tag != 1 || (channels != 1 && channels != 2) ||
        (bits != 8 && bits != 16) || !rate || !align ||
        align != (uint16_t)(channels * bits / 8) || pcm_size % align)
        return NULL;
    wav_decoder_t *decoder = kcalloc(1, sizeof(*decoder));
    if (!decoder)
        return NULL;
    decoder->data = data + pcm_at;
    decoder->base.codec = NULL;
    decoder->bytes = pcm_size;
    decoder->channels = channels;
    decoder->bits = bits;
    decoder->rate = rate;
    decoder->block_align = align;
    format->sample_rate = rate;
    format->channels = 2;
    format->bits_per_sample = 16;
    *total_frames = pcm_size / align;
    return (audio_decoder_t *)decoder;
}

static size_t wav_read(audio_decoder_t *base, int16_t *out, size_t frames)
{
    wav_decoder_t *decoder = (wav_decoder_t *)base;
    size_t available = (decoder->bytes - decoder->cursor) / decoder->block_align;
    if (frames > available) frames = available;
    for (size_t i = 0; i < frames; i++) {
        const uint8_t *p = decoder->data + decoder->cursor;
        int16_t left = decoder->bits == 8 ? (int16_t)(((int)p[0] - 128) << 8)
                                          : (int16_t)le16(p);
        int16_t right = left;
        if (decoder->channels == 2)
            right = decoder->bits == 8 ? (int16_t)(((int)p[1] - 128) << 8)
                                       : (int16_t)le16(p + decoder->bits / 8);
        out[i * 2] = left; out[i * 2 + 1] = right;
        decoder->cursor += decoder->block_align;
    }
    return frames;
}

static void wav_close(audio_decoder_t *decoder) { kfree(decoder); }

static audio_codec_t wav_codec = {
    .name = "wav", .matches = wav_matches, .open = wav_open,
    .read = wav_read, .close = wav_close,
};

void wav_codec_register(void) {
    wav_codec.next = NULL;
    audio_codec_register(&wav_codec);
}
