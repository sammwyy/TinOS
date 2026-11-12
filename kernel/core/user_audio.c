#include <tinos/audio.h>
#include <tinos/kernel.h>
#include <tinos/user_audio.h>

static uint32_t owner_pid;

int user_audio_open(uint32_t pid, uint32_t sample_rate)
{
    if (!pid || (owner_pid && owner_pid != pid))
        return -1;
    audio_format_t format = { sample_rate, 2, 16 };
    if (!audio_stream_start(&format))
        return -1;
    owner_pid = pid;
    klog("[aud ] user pid %u opened stereo PCM at %u Hz\n", pid, sample_rate);
    return 1;
}

int user_audio_write(uint32_t pid, int handle, const int16_t *samples,
                     size_t frames)
{
    if (!pid || pid != owner_pid || handle != 1)
        return -1;
    return (int)audio_stream_write(samples, frames);
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
