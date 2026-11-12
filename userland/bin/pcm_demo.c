#include <stdint.h>
#include <tinos/libc.h>

static uint8_t chunk[1024];

static int play(unsigned channels, unsigned bits)
{
    tinos_pcm_format_t format = { 22050u, channels, bits };
    int handle = tinos_pcm_open(&format);
    if (handle < 0)
        return 1;
    unsigned stride = channels * bits / 8;
    if (tinos_pcm_write(handle, (const void *)0x00100000u, 1) != -1 ||
        tinos_pcm_write(handle, chunk, 257) != -1 ||
        tinos_pcm_wait(handle, UINT32_MAX) != -1)
        return 2;
    for (unsigned cursor = 0; cursor < 4096;) {
        unsigned frames = 256;
        for (unsigned i = 0; i < frames; i++) {
            int value = ((cursor + i) / 25) & 1 ? 8000 : -8000;
            for (unsigned c = 0; c < channels; c++) {
                uint8_t *sample = chunk + i * stride + c * bits / 8;
                if (bits == 8) {
                    sample[0] = (uint8_t)(128 + value / 256);
                } else {
                    sample[0] = (uint8_t)value;
                    sample[1] = (uint8_t)((uint16_t)value >> 8);
                }
            }
        }
        int written = tinos_pcm_write(handle, chunk, frames);
        if (written < 0)
            return 3;
        if (!written) {
            if (tinos_pcm_wait(handle, 4096) < 0)
                return 4;
            continue;
        }
        cursor += (unsigned)written;
    }
    if (tinos_audio_control(handle, TINOS_AUDIO_DRAIN) < 0 ||
        tinos_pcm_wait(handle, 0) < 0 ||
        tinos_audio_control(handle, TINOS_AUDIO_QUEUED) != 0 ||
        tinos_audio_control(handle, TINOS_AUDIO_CLOSE) < 0 ||
        tinos_pcm_wait(handle, 0) != -1)
        return 5;
    return 0;
}

int main(void)
{
    tinos_pcm_format_t bad = { 22050u, 3, 8 };
    if (tinos_pcm_open((const tinos_pcm_format_t *)0x00100000u) != -1 ||
        tinos_pcm_open(&bad) != -1 ||
        tinos_pcm_wait(1, 0) != -1)
        return 1;
    if (play(1, 8) || play(2, 8) || play(1, 16) || play(2, 16))
        return 2;
    const char message[] = "ring3: PCM U8/S16 mono/stereo drained\n";
    tinos_write(1, message, sizeof(message) - 1);
    return 0;
}
