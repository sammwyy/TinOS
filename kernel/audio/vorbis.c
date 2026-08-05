#include <tinos/codec.h>
#include <tinos/heap.h>
#include <tinos/string.h>

#include "../../third_party/stb_vorbis/stb_vorbis.c"

typedef struct {
    audio_decoder_t base;
    stb_vorbis *vorbis;
    const fs_node_t *file;
    uint8_t *input;
    size_t input_capacity;
    size_t input_size;
    size_t input_at;
    size_t file_at;
    uint8_t *owned_data;
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

static audio_decoder_t *vorbis_open_file(const char *path,
                                         const fs_node_t *file,
                                         const uint8_t *probe,
                                         size_t probe_size,
                                         audio_format_t *format,
                                         uint64_t *total_frames)
{
    (void)path;
    if (!file->size || file->size > 32u * 1024u * 1024u)
        return NULL;
    const uint8_t *data = fs_data(file);
    uint8_t *owned = NULL;
    if (!data) {
        owned = kmalloc(file->size);
        if (!owned || !fs_read_at(file, 0, owned, file->size)) {
            kfree(owned);
            return NULL;
        }
        data = owned;
    }
    audio_decoder_t *base = vorbis_open(data, file->size, format, total_frames);
    if (!base) {
        kfree(owned);
        return NULL;
    }
    ((vorbis_decoder_t *)base)->owned_data = owned;
    (void)probe;
    (void)probe_size;
    return base;
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
    kfree(v->owned_data);
    kfree(v);
}

static audio_codec_t vorbis_codec = {
    .name = "vorbis", .matches = vorbis_matches, .open = vorbis_open,
    .open_file = vorbis_open_file,
    .read = vorbis_read, .seek = vorbis_seek, .close = vorbis_close,
};

void vorbis_codec_register(void) { audio_codec_register(&vorbis_codec); }
