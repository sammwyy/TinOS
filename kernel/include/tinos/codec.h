#ifndef TINOS_CODEC_H
#define TINOS_CODEC_H

#include <tinos/audio.h>
#include <tinos/fs.h>

typedef struct audio_decoder audio_decoder_t;

typedef struct audio_codec {
    const char *name;
    bool (*matches)(const char *path, const uint8_t *data, size_t size);
    audio_decoder_t *(*open)(const uint8_t *data, size_t size,
                             audio_format_t *format, uint64_t *total_frames);
    size_t (*read)(audio_decoder_t *decoder, int16_t *stereo, size_t frames);
    void (*close)(audio_decoder_t *decoder);
    struct audio_codec *next;
} audio_codec_t;

struct audio_decoder {
    const audio_codec_t *codec;
};

void audio_codecs_init(void);
bool audio_codec_register(audio_codec_t *codec);
const audio_codec_t *audio_codec_for(const char *path, const uint8_t *data,
                                     size_t size);
audio_decoder_t *audio_decoder_open(const char *path, const uint8_t *data,
                                    size_t size, audio_format_t *format,
                                    uint64_t *total_frames,
                                    const audio_codec_t **codec_out);
size_t audio_decoder_read(audio_decoder_t *decoder, int16_t *stereo,
                          size_t frames);
void audio_decoder_close(audio_decoder_t *decoder);

void wav_codec_register(void);
void vorbis_codec_register(void);

#endif
