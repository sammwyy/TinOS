#ifndef TINOS_USER_AUDIO_H
#define TINOS_USER_AUDIO_H

#include <tinos/types.h>
#include <uapi/tinos/audio.h>

int user_audio_open(uint32_t pid, uint32_t sample_rate);
int user_audio_write(uint32_t pid, int handle, const int16_t *samples,
                     size_t frames);
int user_audio_control(uint32_t pid, int handle, uint32_t command);
void user_audio_release(uint32_t pid);

#endif
