#include <tinos/audio.h>
#include <tinos/kernel.h>
#include <tinos/user_audio.h>

static uint32_t owner_pid;
static tinos_pcm_format_t input_format;

int user_audio_open(uint32_t pid, uint32_t sample_rate)
{
    tinos_pcm_format_t format = { sample_rate, 2, 16 };
    return user_audio_open_format(pid, &format);
}

int user_audio_open_format(uint32_t pid, const tinos_pcm_format_t *format)
{
    if (!pid || (owner_pid && owner_pid != pid))
        return -1;
    if (!format || (format->channels != 1 && format->channels != 2) ||
        (format->bits_per_sample != 8 && format->bits_per_sample != 16))
        return -1;
    audio_format_t output = { format->sample_rate, 2, 16 };
    if (!audio_stream_start(&output))
        return -1;
    owner_pid = pid;
    input_format = *format;
    klog("[aud ] user pid %u opened stereo PCM at %u Hz\n",
         pid, format->sample_rate);
    return 1;
}

int user_audio_frame_bytes(uint32_t pid, int handle)
{
    if (!pid || pid != owner_pid || handle != 1)
        return -1;
    return input_format.channels * input_format.bits_per_sample / 8;
}

int user_audio_write_raw(uint32_t pid, int handle, const uint8_t *data,
                         size_t frames)
{
    int stride = user_audio_frame_bytes(pid, handle);
    if (stride < 0 || !data || !frames || frames > 256)
        return -1;
    int16_t output[256 * 2];
    for (size_t i = 0; i < frames; i++) {
        const uint8_t *input = data + i * (size_t)stride;
        int16_t left = input_format.bits_per_sample == 16 ?
            (int16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8)) :
            (int16_t)(((int)input[0] - 128) * 256);
        int16_t right = left;
        if (input_format.channels == 2) {
            input += input_format.bits_per_sample / 8;
            right = input_format.bits_per_sample == 16 ?
                (int16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8)) :
                (int16_t)(((int)input[0] - 128) * 256);
        }
        output[i * 2] = left;
        output[i * 2 + 1] = right;
    }
    return (int)audio_stream_write(output, frames);
}

int user_audio_wait_ready(uint32_t pid, int handle, uint32_t maximum)
{
    if (user_audio_frame_bytes(pid, handle) < 0 ||
        maximum > audio_stream_capacity_frames())
        return -1;
    return audio_stream_queued_frames() <= maximum ? 1 : 0;
}

int user_audio_control(uint32_t pid, int handle, uint32_t command)
{
    if (!pid || pid != owner_pid || handle != 1)
        return -1;
    switch (command) {
    case TINOS_AUDIO_PAUSE:
        audio_stream_pause(true);
        return 0;
    case TINOS_AUDIO_RESUME:
        audio_stream_pause(false);
        return 0;
    case TINOS_AUDIO_DRAIN:
        audio_stream_drain();
        return 0;
    case TINOS_AUDIO_QUEUED:
        return (int)audio_stream_queued_frames();
    case TINOS_AUDIO_CLOSE:
        user_audio_release(pid);
        return 0;
    default:
        return -1;
    }
}

void user_audio_release(uint32_t pid)
{
    if (pid && pid == owner_pid) {
        audio_stream_stop();
        owner_pid = 0;
    }
}
