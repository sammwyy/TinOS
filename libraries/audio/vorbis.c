#include <tinos/codec.h>
#include <tinos/heap.h>
#include <tinos/string.h>

#include "../../third_party/stb_vorbis/stb_vorbis.c"

#define VORBIS_INPUT_INITIAL 8192
#define VORBIS_INPUT_MAX     65536

typedef struct {
    audio_decoder_t base;
    stb_vorbis *vorbis;
    const fs_node_t *file;
    uint8_t *input;
    size_t input_capacity;
    size_t input_size;
    size_t input_at;
    size_t file_at;
    int16_t pending[4096 * 2];
    size_t pending_frames;
    size_t pending_at;
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

/* Ogg pages carry an absolute granule position (PCM sample count) in their
 * header; the last page in the stream gives the track length without
 * decoding anything. Scanning a small tail window avoids pulling the whole
 * file over a slow transport just to show a duration. */
static uint64_t vorbis_tail_length(const fs_node_t *file)
{
    size_t want = file->size < 65536 ? file->size : 65536;
    if (want < 27)
        return 0;
    uint8_t *tail = kmalloc(want);
    if (!tail)
        return 0;
    if (!fs_read_at(file, file->size - want, tail, want)) {
        kfree(tail);
        return 0;
    }
    uint64_t best = 0;
    for (size_t i = 0; i + 27 <= want; i++) {
        if (memcmp(tail + i, "OggS", 4) != 0)
            continue;
        size_t header_size = 27u + tail[i + 26];
        if (i + header_size > want)
            continue;
        uint32_t lo = (uint32_t)tail[i + 6] | ((uint32_t)tail[i + 7] << 8) |
                      ((uint32_t)tail[i + 8] << 16) |
                      ((uint32_t)tail[i + 9] << 24);
        uint32_t hi = (uint32_t)tail[i + 10] | ((uint32_t)tail[i + 11] << 8) |
                      ((uint32_t)tail[i + 12] << 16) |
                      ((uint32_t)tail[i + 13] << 24);
        if (lo == 0xffffffffu && hi == 0xffffffffu)
            continue;
        best = hi ? 0xfffffffeu : lo;
    }
    kfree(tail);
    return best;
}

static audio_decoder_t *vorbis_open_file(const char *path,
                                         const fs_node_t *file,
                                         const uint8_t *probe,
                                         size_t probe_size,
                                         audio_format_t *format,
                                         uint64_t *total_frames)
{
    (void)path;
    if (!file->size)
        return NULL;

    size_t capacity = probe_size > VORBIS_INPUT_INITIAL ? probe_size
                                                         : VORBIS_INPUT_INITIAL;
    uint8_t *input = NULL;
    stb_vorbis *vorbis = NULL;
    int consumed = 0;
    size_t read_amount = 0;
    for (;;) {
        size_t want = capacity < file->size ? capacity : file->size;
        uint8_t *block = kmalloc(want);
        if (!block)
            return NULL;
        size_t from_probe = want < probe_size ? want : probe_size;
        memcpy(block, probe, from_probe);
        if (want > from_probe &&
            !fs_read_at(file, from_probe, block + from_probe,
                       want - from_probe)) {
            kfree(block);
            return NULL;
        }
        int error = 0;
        vorbis = stb_vorbis_open_pushdata(block, (int)want, &consumed, &error,
                                          NULL);
        if (vorbis) {
            input = block;
            read_amount = want;
            break;
        }
        kfree(block);
        if (error != VORBIS_need_more_data || want >= file->size ||
            capacity >= VORBIS_INPUT_MAX)
            return NULL;
        capacity = capacity * 2 < VORBIS_INPUT_MAX ? capacity * 2
                                                    : VORBIS_INPUT_MAX;
    }

    stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    if (!info.sample_rate || info.channels < 1) {
        stb_vorbis_close(vorbis);
        kfree(input);
        return NULL;
    }
    vorbis_decoder_t *decoder = kcalloc(1, sizeof(*decoder));
    if (!decoder) {
        stb_vorbis_close(vorbis);
        kfree(input);
        return NULL;
    }
    decoder->vorbis = vorbis;
    decoder->file = file;
    decoder->input = input;
    decoder->input_capacity = read_amount;
    size_t leftover = read_amount - (size_t)consumed;
    if (leftover)
        memmove(input, input + consumed, leftover);
    decoder->input_size = leftover;
    decoder->file_at = read_amount;

    format->sample_rate = info.sample_rate;
    format->channels = 2;
    format->bits_per_sample = 16;
    *total_frames = vorbis_tail_length(file);
    return (audio_decoder_t *)decoder;
}

static bool vorbis_more(vorbis_decoder_t *decoder)
{
    size_t left = decoder->input_size - decoder->input_at;
    if (left && decoder->input_at)
        memmove(decoder->input, decoder->input + decoder->input_at, left);
    decoder->input_size = left;
    decoder->input_at = 0;
    if (decoder->file_at >= decoder->file->size) return left != 0;
    size_t room = decoder->input_capacity - decoder->input_size;
    if (!room) return false;
    size_t remaining = decoder->file->size - decoder->file_at;
    size_t want = remaining < room ? remaining : room;
    if (!want || !fs_read_at(decoder->file, decoder->file_at,
                             decoder->input + decoder->input_size, want))
        return left != 0;
    decoder->input_size += want;
    decoder->file_at += want;
    return true;
}

static size_t vorbis_read(audio_decoder_t *decoder, int16_t *out, size_t frames)
{
    vorbis_decoder_t *v = (vorbis_decoder_t *)decoder;
    if (v->file) {
        size_t copied = 0;
        while (copied < frames) {
            while (v->pending_at < v->pending_frames && copied < frames) {
                out[(copied * 2)] = v->pending[v->pending_at * 2];
                out[(copied * 2) + 1] = v->pending[v->pending_at * 2 + 1];
                v->pending_at++; copied++;
            }
            if (copied == frames) break;
            v->pending_at = v->pending_frames = 0;
            int channels = 0, samples = 0;
            float **pcm = NULL;
            int available = (int)(v->input_size - v->input_at);
            int used = stb_vorbis_decode_frame_pushdata(
                v->vorbis, v->input + v->input_at, available,
                &channels, &pcm, &samples);
            if (used) v->input_at += (size_t)used;
            if (samples > 0) {
                size_t count = samples < 4096 ? (size_t)samples : 4096;
                for (size_t i = 0; i < count; i++) {
                    float left = pcm[0][i];
                    float right = channels > 1 ? pcm[1][i] : left;
                    if (left > 1.0f) left = 1.0f;
                    if (left < -1.0f) left = -1.0f;
                    if (right > 1.0f) right = 1.0f;
                    if (right < -1.0f) right = -1.0f;
                    v->pending[i * 2] = (int16_t)(left * 32767.0f);
                    v->pending[i * 2 + 1] = (int16_t)(right * 32767.0f);
                }
                v->pending_frames = count;
                continue;
            }
            if (!used && !vorbis_more(v)) break;
        }
        return copied;
    }
    if (frames > 4096) frames = 4096;
    return (size_t)stb_vorbis_get_samples_short_interleaved(v->vorbis, 2,
                                                            out, (int)frames * 2);
}

static bool vorbis_seek(audio_decoder_t *base, uint64_t frame)
{
    vorbis_decoder_t *decoder = (vorbis_decoder_t *)base;
    if (decoder->file || frame > 0xffffffffu)
        return false;
    decoder->pending_frames = decoder->pending_at = 0;
    return stb_vorbis_seek(decoder->vorbis, (unsigned int)frame) != 0;
}

static void vorbis_close(audio_decoder_t *decoder)
{
    vorbis_decoder_t *v = (vorbis_decoder_t *)decoder;
    stb_vorbis_close(v->vorbis);
    kfree(v->input);
    kfree(v);
}

static audio_codec_t vorbis_codec = {
    .name = "vorbis", .matches = vorbis_matches, .open = vorbis_open,
    .open_file = vorbis_open_file,
    .read = vorbis_read, .seek = vorbis_seek, .close = vorbis_close,
};

void vorbis_codec_register(void) { audio_codec_register(&vorbis_codec); }
