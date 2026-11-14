#include <tinos/string.h>
#include <tinos/widgets.h>
#include <tinos/wav.h>

static tinos_widget_view_t ui;
static const char *track;
static char tracks[12][256];
static unsigned track_count, track_at, playlist_scroll;
static int playlist_open = 1, seeking, volume_drag;
static int audio = -1;
static int fd = -1;
static uint32_t submitted;
static unsigned played_frames;
static tinos_audio_settings_t volume = { .volume = 80 };
static tinos_wav_t wav;
static int16_t samples[256 * 2];
static unsigned pending, pending_at, ticks;
static unsigned total_frames;
static uint16_t levels[24];
static int paused, draining, finished;
static const char *error;

static unsigned playlist_rows(unsigned height)
{
    return height > 200 ? (height - 200) / 20 : 1;
}

static const char *filename(const char *path)
{
    const char *last = path;
    for (const char *at = path; *at; at++)
        if (*at == '/')
            last = at + 1;
    return last;
}

static void add_track(const char *path)
{
    size_t len = strlen(path);
    if (track_count == 12 || len < 4 ||
        strcmp(path + len - 4, ".wav") != 0)
        return;
    tinos_strlcpy(tracks[track_count++], path, sizeof(tracks[0]));
}

static void collect_tracks(const char *requested)
{
    char directory[256];
    tinos_strlcpy(directory, requested, sizeof(directory));
    tinos_dirent_t entry;
    if (tinos_stat(requested, &entry) != 1 ||
        entry.type != TINOS_DIRENT_DIR) {
        size_t len = strlen(directory);
        while (len > 1 && directory[len - 1] != '/')
            len--;
        directory[len > 1 ? len - 1 : 1] = '\0';
    }
    for (unsigned i = 0; i < 128 && track_count < 12; i++) {
        if (tinos_readdir(directory, i, &entry) != 1)
            break;
        if (entry.type != TINOS_DIRENT_FILE)
            continue;
        char path[256];
        tinos_strlcpy(path, directory, sizeof(path));
        size_t len = strlen(path);
        if (len > 1 && len + 1 < sizeof(path))
            path[len++] = '/';
        if (len + strlen(entry.name) >= sizeof(path))
            continue;
        tinos_strlcpy(path + len, entry.name, sizeof(path) - len);
        add_track(path);
        if (strcmp(path, requested) == 0)
            track_at = track_count - 1;
    }
    if (!track_count)
        add_track(requested);
    track = track_count ? tracks[track_at] : requested;
}

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
    pending = pending_at = submitted = played_frames = ticks = 0;
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

static void choose_track(unsigned index)
{
    if (index >= track_count)
        return;
    track_at = index;
    track = tracks[index];
    unsigned rows = playlist_rows(ui.height);
    if (index < playlist_scroll)
        playlist_scroll = index;
    else if (index >= playlist_scroll + rows)
        playlist_scroll = index - rows + 1;
    start_track();
}

static void seek_to(unsigned frame)
{
    if (fd < 0 || !wav.channels || frame > total_frames ||
        frame > (UINT32_MAX - 44u) / (wav.channels * 2u))
        return;
    unsigned was_paused = paused;
    if (tinos_seek(fd, 44u + frame * wav.channels * 2u) < 0)
        return;
    if (audio >= 0)
        tinos_audio_control(audio, TINOS_AUDIO_CLOSE);
    audio = -1;
    tinos_pcm_format_t format = { wav.sample_rate, 2, 16 };
    audio = tinos_pcm_open(&format);
    if (audio < 0) {
        error = "Audio unavailable after seek";
        return;
    }
    wav.frames_remaining = total_frames - frame;
    submitted = frame;
    played_frames = frame;
    pending = pending_at = draining = finished = 0;
    paused = was_paused;
    if (paused)
        tinos_audio_control(audio, TINOS_AUDIO_PAUSE);
}

static void seek_at(int x)
{
    if (!total_frames || x < 16 || x >= (int)ui.width - 16)
        return;
    unsigned bar = ui.width - 32, point = (unsigned)x - 16;
    unsigned frame = total_frames / bar * point +
                     (total_frames % bar) * point / bar;
    seek_to(frame);
}

static void set_volume_at(int x)
{
    tinos_audio_settings_t settings;
    if (tinos_ioctl(0, TINOS_IOCTL_AUDIO_GET,
                    (unsigned)(uintptr_t)&settings) < 0)
        return;
    int value = (x - ((int)ui.width - 104)) * 100 / 88;
    settings.volume = value < 0 ? 0u : value > 100 ? 100u : (unsigned)value;
    tinos_ioctl(0, TINOS_IOCTL_AUDIO_SET,
                (unsigned)(uintptr_t)&settings);
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
    int queued = tinos_audio_control(audio, TINOS_AUDIO_QUEUED);
    if (queued >= 0 && (unsigned)queued <= submitted)
        played_frames = submitted - (unsigned)queued;
    if (draining && queued == 0) {
        finished = 1;
        played_frames = total_frames;
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

static void paint(tinos_widget_view_t *view, tinos_canvas_t *canvas,
                  void *context)
{
    (void)context;
    int w = (int)view->width, h = (int)view->height;
    int progress_y = h - 106, bottom_y = h - 62;
    int top_x = 12, top_w = w - 24;
    int viz_h = progress_y - 48;
    tinos_canvas_fill(canvas, 0, 0, w, h, 0x1a202bu);
    if (playlist_open && w >= 420) {
        tinos_canvas_fill(canvas, top_x, 12, 184, progress_y - 48, 0x242b38u);
        tinos_canvas_frame(canvas, top_x, 12, 184, progress_y - 48,
                           0x4a5973u);
        tinos_canvas_text(canvas, top_x + 12, 24, 160,
                          "Playlist", 0xb5c4d8u);
        unsigned shown = playlist_rows(view->height);
        for (unsigned i = 0; i < shown &&
             playlist_scroll + i < track_count; i++) {
            unsigned index = playlist_scroll + i;
            int y = 45 + (int)i * 20;
            if (index == track_at)
                tinos_canvas_fill(canvas, top_x + 4, y,
                                  176, 20, view->theme.selection);
            tinos_canvas_text(canvas, top_x + 12, y + 3, 160,
                              filename(tracks[index]),
                              index == track_at ? 0xffffffu : 0xd1d7e2u);
        }
        top_x += 196;
        top_w -= 196;
    }
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
    timestamp(elapsed, played_frames);
    timestamp(total, total_frames);
    tinos_canvas_text(canvas, 16, progress_y - 11, 55, elapsed,
                      0xb5c4d8u);
    tinos_canvas_text(canvas, w - 56, progress_y - 11, 44, total,
                      0xb5c4d8u);
    tinos_canvas_fill(canvas, 16, progress_y, w - 32, 14, 0x181d28u);
    unsigned bar = (unsigned)(w - 32);
    unsigned played = played_frames < total_frames ?
                      played_frames : total_frames;
    int fill = !total_frames ? 0 :
        played <= UINT32_MAX / bar ?
        (int)(played * bar / total_frames) :
        (int)(played / (total_frames / bar));
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
        "prev", paused || finished ? "play" : "pause", "stop", "next",
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
    tinos_canvas_fill(canvas, w - 102, bottom_y + 14,
                      84 * (int)volume.volume / 100, 5,
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
    tinos_ioctl(0, TINOS_IOCTL_AUDIO_GET, (unsigned)(uintptr_t)&volume);
    ui.dirty = true;
    tinos_widget_flush(&ui);
}

int main(int argc, char **argv)
{
    collect_tracks(argc > 1 && argv[1][0] == '/' ?
                   argv[1] : "/apps/media/demo.wav");
    tinos_system_info_t system;
    bool roomy = tinos_sysinfo(&system) == 0 &&
                 system.memory_total_kib >= 12288;
    if (tinos_widget_open(&ui, "Media Player",
                          roomy ? 600 : 440, roomy ? 360 : 280) < 0)
        return 1;
    ui.on_paint = paint;
    unsigned rows = playlist_rows(ui.height);
    if (track_at >= rows)
        playlist_scroll = track_at - rows + 1;
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
            } else if (event.ch == ']' && track_count) {
                choose_track((track_at + 1) % track_count);
                render();
            } else if (event.ch == '[' && track_count) {
                choose_track((track_at + track_count - 1) % track_count);
                render();
            } else if (event.ch == 'o' || event.ch == 'O') {
                playlist_open = !playlist_open;
                render();
            } else if (event.ch == ' ' && (audio >= 0 || finished)) {
                if (finished)
                    start_track();
                else if (audio >= 0) {
                    paused = !paused;
                    tinos_audio_control(audio, paused ? TINOS_AUDIO_PAUSE :
                                                         TINOS_AUDIO_RESUME);
                }
                render();
            } else if (event.code == TINOS_KEY_LEFT ||
                       event.code == TINOS_KEY_RIGHT) {
                unsigned delta = wav.sample_rate * 5u;
                unsigned frame = event.code == TINOS_KEY_RIGHT ?
                    (total_frames - submitted < delta ? total_frames :
                     submitted + delta) :
                    (submitted > delta ? submitted - delta : 0);
                seek_to(frame);
                render();
            }
        } else if (event.type == TINOS_GUI_RESIZE) {
            render();
        } else if (event.type == TINOS_GUI_MOUSE) {
            int transport_x = (int)ui.width / 2 - 70;
            int bottom_y = (int)ui.height - 62;
            int progress_y = (int)ui.height - 106;
            if ((int32_t)event.ch && playlist_open && ui.width >= 420 &&
                event.x >= 16 && event.x < 192 && event.y >= 45 &&
                event.y < progress_y - 36) {
                int next = (int)playlist_scroll +
                           ((int32_t)event.ch > 0 ? -1 : 1);
                int max = (int)track_count - (int)playlist_rows(ui.height);
                if (next < 0) next = 0;
                if (next > max) next = max > 0 ? max : 0;
                playlist_scroll = (unsigned)next;
                render();
            } else if (seeking && (event.mods >> 8 & TINOS_GUI_MOUSE_LEFT)) {
                seeking = 0;
                seek_at(event.x);
                render();
            } else if (volume_drag &&
                       (event.mods & TINOS_GUI_MOUSE_LEFT ||
                        event.mods >> 8 & TINOS_GUI_MOUSE_LEFT)) {
                set_volume_at(event.x);
                if (event.mods >> 8 & TINOS_GUI_MOUSE_LEFT)
                    volume_drag = 0;
                render();
            } else if (event.code & TINOS_GUI_MOUSE_LEFT &&
                       event.y >= progress_y &&
                       event.y < progress_y + 14) {
                seeking = 1;
            } else if (event.code & TINOS_GUI_MOUSE_LEFT &&
                       event.x >= (int)ui.width - 104 &&
                       event.x < (int)ui.width - 16 &&
                       event.y >= bottom_y + 9 &&
                       event.y < bottom_y + 25) {
                volume_drag = 1;
                set_volume_at(event.x);
                render();
            } else if (event.code & TINOS_GUI_MOUSE_LEFT &&
                       playlist_open && ui.width >= 420 &&
                       event.x >= 16 && event.x < 192 &&
                       event.y >= 45 && event.y < progress_y - 36) {
                unsigned target = playlist_scroll + (event.y - 45) / 20;
                if (target < track_count) {
                    choose_track(target);
                    render();
                }
            } else if (event.code & TINOS_GUI_MOUSE_LEFT &&
                       event.y >= bottom_y && event.y < bottom_y + 34) {
                if (event.x >= 16 && event.x < 50) {
                    playlist_open = !playlist_open;
                    render();
                }
                if (event.x >= transport_x &&
                    event.x < transport_x + 34) {
                    choose_track(track_count ?
                        (track_at + track_count - 1) % track_count : 0);
                    render();
                } else if (event.x >= transport_x + 40 &&
                           event.x < transport_x + 82 &&
                           (audio >= 0 || finished)) {
                    if (finished)
                        start_track();
                    else {
                        paused = !paused;
                        tinos_audio_control(audio, paused ? TINOS_AUDIO_PAUSE :
                                                         TINOS_AUDIO_RESUME);
                    }
                    render();
                } else if (event.x >= transport_x + 88 &&
                           event.x < transport_x + 122) {
                    close_track();
                    finished = 1;
                    render();
                } else if (event.x >= transport_x + 128 &&
                           event.x < transport_x + 162) {
                    choose_track(track_count ?
                        (track_at + 1) % track_count : 0);
                    render();
                }
            }
        }
    }
    close_track();
    tinos_widget_close(&ui);
    return 0;
}
