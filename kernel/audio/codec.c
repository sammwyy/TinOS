#include <tinos/codec.h>
#include <tinos/heap.h>
#include <tinos/string.h>

static audio_codec_t *codecs;

void audio_codecs_init(void)
{
    codecs = NULL;
    wav_codec_register();
    vorbis_codec_register();
}

bool audio_codec_register(audio_codec_t *codec)
{
    if (!codec || !codec->name || !codec->matches || !codec->open ||
        !codec->read || !codec->close)
        return false;
    for (audio_codec_t *it = codecs; it; it = it->next)
        if (strcmp(it->name, codec->name) == 0)
            return false;
    codec->next = codecs;
    codecs = codec;
    return true;
}

const audio_codec_t *audio_codec_for(const char *path, const uint8_t *data,
                                     size_t size)
{
    for (audio_codec_t *it = codecs; it; it = it->next)
        if (it->matches(path, data, size))
            return it;
    return NULL;
}

audio_decoder_t *audio_decoder_open(const char *path, const uint8_t *data,
                                    size_t size, audio_format_t *format,
                                    uint64_t *total_frames,
                                    const audio_codec_t **codec_out)
{
    const audio_codec_t *codec = audio_codec_for(path, data, size);
    if (!codec)
        return NULL;
    audio_decoder_t *decoder = codec->open(data, size, format, total_frames);
    if (decoder) {
        decoder->codec = codec;
        if (codec_out)
            *codec_out = codec;
    }
    return decoder;
}

audio_decoder_t *audio_decoder_open_file(const char *path,
                                         const fs_node_t *file,
                                         audio_format_t *format,
                                         uint64_t *total_frames,
                                         const audio_codec_t **codec_out)
{
    if (!file || file->type != FS_FILE || !file->size)
        return NULL;
    size_t size = file->size < 4096 ? file->size : 4096;
    uint8_t *probe = kmalloc(size);
    if (!probe || !fs_read_at(file, 0, probe, size)) {
        kfree(probe);
        return NULL;
    }
    for (audio_codec_t *codec = codecs; codec; codec = codec->next) {
        if (!codec->matches(path, probe, size))
            continue;
        audio_decoder_t *decoder = codec->open_file
            ? codec->open_file(path, file, probe, size, format, total_frames)
            : audio_decoder_open(path, fs_data(file), file->size,
                                 format, total_frames, codec_out);
        kfree(probe);
        if (decoder) {
            decoder->codec = codec;
            if (codec_out) *codec_out = codec;
        }
        return decoder;
    }
    return NULL;
}

size_t audio_decoder_read(audio_decoder_t *decoder, int16_t *stereo,
                          size_t frames)
{
    if (!decoder || !stereo || !frames)
        return 0;
    return decoder->codec->read(decoder, stereo, frames);
}

void audio_decoder_close(audio_decoder_t *decoder)
{
    if (!decoder)
        return;
    decoder->codec->close(decoder);
}
