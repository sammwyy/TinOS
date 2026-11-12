#ifndef TINOS_USER_AUDIO_H
#define TINOS_USER_AUDIO_H

#include <tinos/types.h>
#include <uapi/tinos/audio.h>

int user_audio_open(uint32_t pid, uint32_t sample_rate);
int user_audio_open_format(uint32_t pid, const tinos_pcm_format_t *format);
int user_audio_frame_bytes(uint32_t pid, int handle);
int user_audio_write_raw(uint32_t pid, int handle, const uint8_t *data,
                         size_t frames);
int user_audio_wait_ready(uint32_t pid, int handle, uint32_t maximum);
int user_audio_control(uint32_t pid, int handle, uint32_t command);
void user_audio_release(uint32_t pid);

#endif
