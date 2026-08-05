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
    const fs_node_t *file;
    size_t data_offset;
    uint8_t *chunk;
    size_t chunk_at;
    size_t chunk_size;
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

static audio_decoder_t *wav_open_impl(const uint8_t *data, size_t size,
                                      audio_format_t *format,
                                      uint64_t *total_frames,
                                      size_t *data_offset, bool partial)
{
    if (!wav_matches(NULL, data, size))
        return NULL;
    uint32_t riff_size = le32(data + 4);
    if (riff_size < 4 || (!partial && riff_size > size - 8))
        return NULL;
    size_t at = 12, fmt_at = 0, fmt_size = 0, pcm_at = 0, pcm_size = 0;
    while (at + 8 <= size) {
        uint32_t chunk = le32(data + at + 4);
        size_t next = at + 8u + chunk + (chunk & 1u);
        if (next < at + 8 || (next > size &&
                              (!partial || !tag(data + at, "data"))))
            return NULL;
        if (tag(data + at, "fmt ")) { fmt_at = at + 8; fmt_size = chunk; }
        if (tag(data + at, "data")) { pcm_at = at + 8; pcm_size = chunk; }
        if (partial && tag(data + at, "data")) break;
        at = next;
    }
    if (!fmt_at || !pcm_at || fmt_size < 16 || pcm_size == 0 ||
        fmt_at + fmt_size > size || (!partial && pcm_at + pcm_size > size))
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
    if (data_offset) *data_offset = pcm_at;
    return (audio_decoder_t *)decoder;
}

static audio_decoder_t *wav_open(const uint8_t *data, size_t size,
                                 audio_format_t *format,
                                 uint64_t *total_frames)
{
    return wav_open_impl(data, size, format, total_frames, NULL, false);
}

static audio_decoder_t *wav_open_file(const char *path, const fs_node_t *file,
                                      const uint8_t *probe, size_t probe_size,
                                      audio_format_t *format,
                                      uint64_t *total_frames)
{
    (void)path;
    size_t data_offset = 0;
    wav_decoder_t *decoder = (wav_decoder_t *)wav_open_impl(
        probe, probe_size, format, total_frames, &data_offset, true);
    if (!decoder) return NULL;
    if (data_offset > file->size || decoder->bytes > file->size - data_offset) {
        kfree(decoder);
        return NULL;
    }
    decoder->data = NULL;
    decoder->file = file;
    decoder->data_offset = data_offset;
    decoder->chunk = kmalloc(32768);
    if (!decoder->chunk) { kfree(decoder); return NULL; }
    size_t available = probe_size - data_offset;
    size_t seed = decoder->bytes < available ? decoder->bytes : available;
    if (seed > 32768)
        seed = 32768;
    memcpy(decoder->chunk, probe + data_offset, seed);
    decoder->chunk_size = seed;
    return (audio_decoder_t *)decoder;
}

static size_t wav_read(audio_decoder_t *base, int16_t *out, size_t frames)
{
    wav_decoder_t *decoder = (wav_decoder_t *)base;
    size_t available = (decoder->bytes - decoder->cursor) / decoder->block_align;
    if (frames > available) frames = available;
    for (size_t i = 0; i < frames; i++) {
        const uint8_t *p;
        if (decoder->file) {
            size_t absolute = decoder->cursor;
            if (absolute < decoder->chunk_at ||
                absolute >= decoder->chunk_at + decoder->chunk_size) {
                decoder->chunk_at = absolute & ~(size_t)32767;
                size_t remaining = decoder->bytes - decoder->chunk_at;
                size_t want = remaining < (size_t)32768 ? remaining : 32768;
                if (!fs_read_at(decoder->file,
                                decoder->data_offset + decoder->chunk_at,
                                decoder->chunk, want)) return i;
                decoder->chunk_size = want;
            }
            p = decoder->chunk + (absolute - decoder->chunk_at);
        } else {
            p = decoder->data + decoder->cursor;
        }
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

static void wav_close(audio_decoder_t *decoder)
{
    wav_decoder_t *wav = (wav_decoder_t *)decoder;
    kfree(wav->chunk);
    kfree(wav);
}

static audio_codec_t wav_codec = {
    .name = "wav", .matches = wav_matches, .open = wav_open,
    .open_file = wav_open_file,
    .read = wav_read, .close = wav_close,
};

void wav_codec_register(void) {
    wav_codec.next = NULL;
    audio_codec_register(&wav_codec);
}
