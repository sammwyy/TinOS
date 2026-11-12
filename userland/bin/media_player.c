#include <tinos/string.h>
#include <tinos/ui.h>
#include <tinos/wav.h>

static tinos_ui_t ui;
static const char *track;
static int audio = -1;
static int fd = -1;
static uint32_t submitted;
static tinos_wav_t wav;
static int16_t samples[256 * 2];
static unsigned pending, pending_at, ticks;
static int paused, draining, finished;
static const char *error;

static int read_file(void *context, void *buffer, unsigned length)
{
    return tinos_read(*(int *)context, buffer, length);
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
    if (tinos_wav_open(read_file, &fd, &wav) < 0) {
        error = "Expected standard PCM16 WAV";
        close_track();
        return;
    }
    tinos_pcm_format_t format = { wav.sample_rate, 2, 16 };
    audio = tinos_pcm_open(&format);
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
        if (!pending && wav.frames_remaining) {
            int frames = tinos_wav_read(read_file, &fd, &wav, samples, 256);
            if (frames <= 0) {
                error = "Unexpected end of WAV data";
                close_track();
                return;
            }
            pending = (unsigned)frames;
            pending_at = 0;
        }
        if (!pending)
            break;
        int sent = tinos_pcm_write(audio, samples + pending_at * 2, pending);
        if (sent <= 0)
            break;
        pending -= (unsigned)sent;
        pending_at += (unsigned)sent;
        submitted += (unsigned)sent;
    }
    if (!wav.frames_remaining && !pending && !draining) {
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
