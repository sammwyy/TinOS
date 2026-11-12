#include <tinos/wav.h>

static uint16_t le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int exact(tinos_wav_reader_t read, void *context,
                 uint8_t *bytes, unsigned length)
{
    while (length) {
        int got = read(context, bytes, length);
        if (got <= 0 || (unsigned)got > length)
            return -1;
        bytes += got;
        length -= (unsigned)got;
    }
    return 0;
}

int tinos_wav_open(tinos_wav_reader_t read, void *context, tinos_wav_t *wav)
{
    if (!read || !wav)
        return -1;
    uint8_t header[44];
    if (exact(read, context, header, sizeof(header)) < 0)
        return -1;
    const char riff[] = "RIFF", wave[] = "WAVE";
    const char fmt[] = "fmt ", data[] = "data";
    for (unsigned i = 0; i < 4; i++)
        if (header[i] != riff[i] || header[i + 8] != wave[i] ||
            header[i + 12] != fmt[i] || header[i + 36] != data[i])
            return -1;
    unsigned channels = le16(header + 22);
    unsigned rate = le32(header + 24);
    unsigned bytes = le32(header + 40);
    if (le32(header + 16) != 16 || le16(header + 20) != 1 ||
        (channels != 1 && channels != 2) || le16(header + 34) != 16 ||
        rate < 8000 || rate > 48000 || le16(header + 32) != channels * 2 ||
        !bytes || bytes % (channels * 2))
        return -1;
    *wav = (tinos_wav_t){
        .sample_rate = rate, .channels = channels,
        .frames_remaining = bytes / (channels * 2),
    };
    return 0;
}

int tinos_wav_read(tinos_wav_reader_t read, void *context, tinos_wav_t *wav,
                   int16_t *stereo, unsigned capacity_frames)
{
    if (!read || !wav || !stereo || !capacity_frames)
        return -1;
    unsigned frames = capacity_frames;
    if (frames > wav->frames_remaining)
        frames = wav->frames_remaining;
    if (frames > 256)
        frames = 256;
    if (!frames)
        return 0;
    uint8_t input[1024];
    unsigned frame_bytes = wav->channels * 2;
    if (exact(read, context, input, frames * frame_bytes) < 0)
        return -1;
    for (unsigned i = 0; i < frames; i++) {
        int16_t left = (int16_t)le16(input + i * frame_bytes);
        int16_t right = wav->channels == 2 ?
            (int16_t)le16(input + i * frame_bytes + 2) : left;
        stereo[i * 2] = left;
        stereo[i * 2 + 1] = right;
    }
    wav->frames_remaining -= frames;
    return (int)frames;
}
