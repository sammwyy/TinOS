#include <tinos/string.h>
#include <tinos/widgets.h>
#include <tinos/wav.h>

static tinos_widget_view_t ui;
static const char *track;
static int audio = -1;
static int fd = -1;
static uint32_t submitted;
static tinos_wav_t wav;
static int16_t samples[256 * 2];
static unsigned pending, pending_at, ticks;
static unsigned total_frames;
static uint16_t levels[24];
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
    total_frames = 0;
    memset(levels, 0, sizeof(levels));
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
    total_frames = wav.frames_remaining;
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
            for (int bin = 0; bin < 24; bin++) {
                unsigned start = (unsigned)bin * pending / 24;
                unsigned end = (unsigned)(bin + 1) * pending / 24;
                int peak = 0;
                for (unsigned at = start; at < end; at++) {
                    int value = samples[at * 2];
                    if (value < 0)
                        value = -value;
                    if (value > peak)
                        peak = value;
                }
                levels[bin] = (uint16_t)peak;
            }
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

static void timestamp(char text[16], unsigned frames)
{
    unsigned seconds = wav.sample_rate ? frames / wav.sample_rate : 0;
    char number[11];
    tinos_u32(text, seconds / 60);
    tinos_strlcpy(text + strlen(text), ":", 16 - strlen(text));
    tinos_u32(number, seconds % 60);
    if (seconds % 60 < 10)
        tinos_strlcpy(text + strlen(text), "0", 16 - strlen(text));
    tinos_strlcpy(text + strlen(text), number, 16 - strlen(text));
}

static const char *filename(const char *path)
{
    const char *last = path;
    for (const char *at = path; *at; at++)
        if (*at == '/')
            last = at + 1;
    return last;
}

static void paint(tinos_widget_view_t *view, tinos_canvas_t *canvas,
                  void *context)
{
    (void)context;
    int w = (int)view->width, h = (int)view->height;
    int progress_y = h - 106, bottom_y = h - 62;
    int top_x = 12, top_w = w - 24;
    tinos_canvas_fill(canvas, 0, 0, w, h, 0x1a202bu);
    if (w >= 570) {
        tinos_canvas_fill(canvas, top_x, 12, 184, progress_y - 48, 0x242b38u);
        tinos_canvas_frame(canvas, top_x, 12, 184, progress_y - 48,
                           0x4a5973u);
        tinos_canvas_text(canvas, top_x + 12, 24, 160,
                          "Playlist", 0xb5c4d8u);
        tinos_canvas_fill(canvas, top_x + 4, 45, 176, 20,
                          view->theme.selection);
        tinos_canvas_text(canvas, top_x + 12, 48, 160,
                          filename(track), 0xffffffu);
        top_x += 196;
        top_w -= 196;
    }
    int viz_h = progress_y - 48;
    tinos_canvas_fill(canvas, top_x, 12, top_w, viz_h, 0x0d121cu);
    tinos_canvas_frame(canvas, top_x, 12, top_w, viz_h, 0x3a4b68u);
    int bar_width = (top_w - 46) / 24;
    if (bar_width < 4) bar_width = 4;
    int step = bar_width + 2;
    int block = bar_width > 9 ? 9 : bar_width;
    int max_blocks = (viz_h - 8) / (block + 1);
    for (int bin = 0; bin < 24; bin++) {
        int blocks = levels[bin] * max_blocks / 32768;
        uint32_t color = (uint32_t)(0x35 + bin * 7) << 16 |
                         (uint32_t)(0xd0 - bin * 4) << 8 |
                         (uint32_t)(0xd0 + bin / 5);
        for (int row = 0; row < blocks; row++)
            tinos_canvas_fill(canvas, top_x + bin * step, 12 + viz_h - 5 -
                              (row + 1) * (block + 1), block, block, color);
    }
    tinos_canvas_text(canvas, 64, progress_y - 27, w - 128,
                      filename(track), 0xe5ebf5u);
    char elapsed[16], total[16];
    timestamp(elapsed, submitted);
    timestamp(total, total_frames);
    tinos_canvas_text(canvas, 16, progress_y - 11, 55, elapsed,
                      view->theme.muted);
    tinos_canvas_text(canvas, w - 56, progress_y - 11, 44, total,
                      view->theme.muted);
    tinos_canvas_fill(canvas, 16, progress_y, w - 32, 14, 0x181d28u);
    int fill = total_frames ? submitted * (unsigned)(w - 32) / total_frames : 0;
    if (fill > w - 32)
        fill = w - 32;
    tinos_canvas_fill(canvas, 16, progress_y, fill, 14,
                      view->theme.accent_light);
    tinos_canvas_frame(canvas, 16, progress_y, w - 32, 14,
                       view->theme.border);
    tinos_canvas_button(canvas, 16, bottom_y, 34, 34, NULL, false);
    tinos_canvas_icon(canvas, "folder", 25, bottom_y + 9, 16);
    int transport_x = w / 2 - 70;
    const int offsets[] = { 0, 40, 88, 128 };
    const char *const icons[] = {
        "back", paused || finished ? "play" : "pause", "power", "forward",
    };
    for (int i = 0; i < 4; i++) {
        int size = i == 1 ? 42 : 34;
        tinos_canvas_button(canvas, transport_x + offsets[i], bottom_y,
                            size, 34, NULL, false);
        tinos_canvas_icon(canvas, icons[i],
                          transport_x + offsets[i] + (size - 16) / 2,
                          bottom_y + 9, 16);
    }
    tinos_canvas_icon(canvas, "volume", w - 126, bottom_y + 9, 16);
    tinos_canvas_fill(canvas, w - 104, bottom_y + 12, 88, 9,
                      view->theme.border);
    tinos_canvas_fill(canvas, w - 102, bottom_y + 14, 52, 5,
                      view->theme.accent_light);
    const char *state = error ? error :
                        finished ? "Finished" : paused ? "Paused" : "Playing";
    tinos_canvas_text(canvas, 16, h - 20, w - 150, "PCM WAV",
                      0xd1d7e2u);
    tinos_canvas_text(canvas, w - 144, h - 20, 136, state,
                      error ? view->theme.danger : 0xd1d7e2u);
}

static void render(void)
{
    ui.dirty = true;
    tinos_widget_flush(&ui);
}

int main(int argc, char **argv)
{
    track = argc > 1 && argv[1][0] == '/' ?
            argv[1] : "/apps/media/demo.wav";
    tinos_system_info_t system;
    bool roomy = tinos_sysinfo(&system) == 0 &&
                 system.memory_total_kib >= 12288;
    if (tinos_widget_open(&ui, "Media Player",
                          roomy ? 600 : 440, roomy ? 360 : 280) < 0)
        return 1;
    ui.on_paint = paint;
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
            } else if (event.ch == ' ' && (audio >= 0 || finished)) {
                if (finished)
                    start_track();
                if (audio < 0)
                    continue;
                paused = !paused;
                tinos_audio_control(audio, paused ? TINOS_AUDIO_PAUSE :
                                                     TINOS_AUDIO_RESUME);
                render();
            }
        } else if (event.type == TINOS_GUI_RESIZE) {
            render();
        } else if (event.type == TINOS_GUI_MOUSE && event.code == 1) {
            int transport_x = (int)ui.width / 2 - 70;
            int bottom_y = (int)ui.height - 62;
            if (event.y >= bottom_y && event.y < bottom_y + 34) {
                if (event.x >= transport_x &&
                    event.x < transport_x + 34) {
                    start_track();
                    render();
                } else if (event.x >= transport_x + 40 &&
                           event.x < transport_x + 82 && audio >= 0) {
                    paused = !paused;
                    tinos_audio_control(audio, paused ? TINOS_AUDIO_PAUSE :
                                                     TINOS_AUDIO_RESUME);
                    render();
                } else if (event.x >= transport_x + 88 &&
                           event.x < transport_x + 122) {
                    close_track();
                    finished = 1;
                    render();
                }
            }
        }
    }
    close_track();
    tinos_widget_close(&ui);
    return 0;
}
