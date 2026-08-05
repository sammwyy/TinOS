#ifndef TINOS_AUDIO_H
#define TINOS_AUDIO_H

#include <tinos/types.h>

bool audio_available(void);
bool audio_play_doremi(void);
typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
} audio_format_t;

bool audio_stream_start(const audio_format_t *format);
size_t audio_stream_write(const int16_t *samples, size_t frames);
void audio_stream_pause(bool paused);
void audio_stream_drain(void);
void audio_stream_stop(void);
size_t audio_stream_queued_frames(void);
size_t audio_stream_capacity_frames(void);
uint64_t audio_stream_position(void);
void audio_poll(void);
void audio_driver_register(void);

#endif
