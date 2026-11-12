#include <tinos/string.h>
#include <tinos/ui.h>

static tinos_ui_t ui;
static const char *track;
static int audio = -1;
static int fd = -1;
static uint32_t remaining, submitted;
static unsigned channels, frame_bytes;
static int16_t samples[256 * 2];
static uint8_t input[1024];
static unsigned pending, pending_at, ticks;
static int paused, draining, finished;
static const char *error;

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int read_exact(int stream, void *output, unsigned length)
{
    uint8_t *bytes = output;
    while (length) {
        unsigned want = length > 1024 ? 1024 : length;
        int got = tinos_read(stream, bytes, want);
        if (got <= 0)
            return -1;
        bytes += got;
        length -= got;
    }
    return 0;
}

static void close_track(void)
{
    if (audio >= 0)
        tinos_audio_control(audio, TINOS_AUDIO_CLOSE);
    if (fd >= 0)
        tinos_close(fd);
    fd = audio = -1;
}

static void start_track(void)
{
    close_track();
    error = NULL;
    pending = pending_at = submitted = ticks = 0;
    paused = draining = finished = 0;
    fd = tinos_open(track);
    if (fd < 0) {
        error = "Cannot open WAV file";
        return;
    }
    uint8_t header[44];
    if (read_exact(fd, header, sizeof(header)) < 0 ||
        memcmp(header, "RIFF", 4) || memcmp(header + 8, "WAVE", 4) ||
        memcmp(header + 12, "fmt ", 4) ||
        read_u32(header + 16) != 16 ||
        read_u16(header + 20) != 1 ||
        memcmp(header + 36, "data", 4)) {
        error = "Only standard PCM WAV files work";
        close_track();
        return;
    }
    channels = read_u16(header + 22);
    unsigned bits = read_u16(header + 34);
    uint32_t rate = read_u32(header + 24);
    frame_bytes = channels * 2;
    remaining = read_u32(header + 40);
    if ((channels != 1 && channels != 2) || bits != 16 ||
        rate < 8000 || rate > 48000 ||
        read_u16(header + 32) != frame_bytes ||
        !remaining || remaining % frame_bytes) {
        error = "Expected 16-bit mono/stereo PCM";
        close_track();
        return;
    }
    audio = tinos_audio_open(rate);
    if (audio < 0) {
        error = "AC97 audio unavailable or busy";
        close_track();
    }
}

static void send_samples(void)
{
    if (audio < 0 || paused || error || finished)
        return;
    for (int i = 0; i < 5; i++) {
        if (!pending && remaining) {
            unsigned frames = remaining / frame_bytes;
            if (frames > 256)
                frames = 256;
            if (read_exact(fd, input, frames * frame_bytes) < 0) {
                error = "Unexpected end of WAV data";
                close_track();
                return;
            }
            for (unsigned f = 0; f < frames; f++) {
                int16_t left = (int16_t)read_u16(input + f * frame_bytes);
                int16_t right = channels == 2 ?
                    (int16_t)read_u16(input + f * frame_bytes + 2) : left;
                samples[f * 2] = left;
                samples[f * 2 + 1] = right;
            }
            remaining -= frames * frame_bytes;
            pending = frames;
            pending_at = 0;
        }
        if (!pending)
            break;
        int sent = tinos_audio_write(audio, samples + pending_at * 2, pending);
        if (sent <= 0)
            break;
        pending -= (unsigned)sent;
        pending_at += (unsigned)sent;
        submitted += (unsigned)sent;
    }
    if (!remaining && !pending && !draining) {
        tinos_audio_control(audio, TINOS_AUDIO_DRAIN);
        draining = 1;
    }
    if (draining && tinos_audio_control(audio, TINOS_AUDIO_QUEUED) == 0) {
        finished = 1;
        close_track();
    }
}

static void render(void)
{
    tinos_ui_clear(&ui);
    tinos_ui_text(&ui, 2, 1, "Media Player - PCM WAV");
    tinos_ui_text(&ui, 2, 3, track);
    tinos_ui_text(&ui, 2, 5, "Status:");
    tinos_ui_text(&ui, 11, 5, error ? error :
                  finished ? "Finished" : paused ? "Paused" : "Playing");
    char number[11];
    tinos_u32(number, submitted);
    tinos_ui_text(&ui, 2, 7, "Frames queued from file:");
    tinos_ui_text(&ui, 27, 7, number);
    tinos_ui_text(&ui, 2, ui.rows - 2, "Space pause  R restart  Esc close");
    tinos_ui_present(&ui);
}

int main(int argc, char **argv)
{
    track = argc > 1 && argv[1][0] == '/' ?
            argv[1] : "/apps/media/demo.wav";
    if (tinos_ui_open(&ui, "Media Player", 440, 260) < 0)
        return 1;
    if (tinos_gui_timer(ui.handle, 40) < 0)
        return 1;
    start_track();
    render();
    tinos_gui_event_t event;
    while (tinos_gui_event(ui.handle, &event) == 1) {
        if (event.type == TINOS_GUI_CLOSE ||
            (event.type == TINOS_GUI_KEY &&
             event.code == TINOS_KEY_ESCAPE))
            break;
        if (event.type == TINOS_GUI_TIMER) {
            send_samples();
            if (++ticks % 5 == 0 || finished || error)
                render();
        } else if (event.type == TINOS_GUI_KEY) {
            if (event.ch == 'r' || event.ch == 'R') {
                start_track();
                render();
            } else if (event.ch == ' ' && audio >= 0) {
                paused = !paused;
                tinos_audio_control(audio, paused ? TINOS_AUDIO_PAUSE :
                                                     TINOS_AUDIO_RESUME);
                render();
            }
        }
    }
    close_track();
    tinos_gui_close(ui.handle);
    return 0;
}
