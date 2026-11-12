#ifndef TINOS_UAPI_AUDIO_H
#define TINOS_UAPI_AUDIO_H

#include <stdint.h>

/* Input to the PCM device: interleaved unsigned 8-bit or signed LE 16-bit.
 * Mono is duplicated to stereo by the driver boundary. */
typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
} tinos_pcm_format_t;

#define TINOS_AUDIO_PAUSE  1u
#define TINOS_AUDIO_RESUME 2u
#define TINOS_AUDIO_DRAIN  3u
#define TINOS_AUDIO_QUEUED 4u
#define TINOS_AUDIO_CLOSE  5u

#endif
