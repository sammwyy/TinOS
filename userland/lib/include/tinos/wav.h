#ifndef TINOS_USER_WAV_H
#define TINOS_USER_WAV_H

#include <stdint.h>

typedef int (*tinos_wav_reader_t)(void *context, void *buffer, unsigned count);

typedef struct {
    uint32_t sample_rate;
    uint32_t frames_remaining;
    uint16_t channels;
} tinos_wav_t;

/* Supports standard 44-byte, 16-bit PCM WAV. Leaves the reader at the PCM. */
int tinos_wav_open(tinos_wav_reader_t read, void *context, tinos_wav_t *wav);
/* Converts mono/stereo input to interleaved stereo signed 16-bit PCM. */
int tinos_wav_read(tinos_wav_reader_t read, void *context, tinos_wav_t *wav,
                   int16_t *stereo, unsigned capacity_frames);

#endif
