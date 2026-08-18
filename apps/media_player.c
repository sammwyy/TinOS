#include <tinos/audio.h>
#include <tinos/arch.h>
#include <tinos/codec.h>
#include <tinos/fs.h>
#include <tinos/gfx.h>
#include <tinos/heap.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/printf.h>
#include <tinos/spinlock.h>
#include <tinos/string.h>
#include <tinos/thread.h>
#include <tinos/ui.h>

#define PLAYER_MAX_TRACKS 64
#define PLAYER_PATH_MAX  FS_PATH_MAX
#define PLAYER_H 30
#define PLAYER_PCM_FRAMES 65536u
#define PLAYER_PREBUFFER_FRAMES 8192u
#define PLAYER_DECODE_FRAMES 512u

typedef enum {
    PLAYER_STOPPED, PLAYER_LOADING, PLAYER_PLAYING, PLAYER_PAUSED, PLAYER_ERROR
} player_state_t;
typedef struct {
    char path[PLAYER_PATH_MAX];
    char name[FS_NAME_MAX];
} player_track_t;
typedef struct {
    player_track_t tracks[PLAYER_MAX_TRACKS];
    size_t count, current;
    audio_decoder_t *decoder;
    const audio_codec_t *codec;
    audio_format_t format;
    uint64_t total, decoded;
    player_state_t state;
    char message[96];
    int hot;
    kernel_thread_t *loader;
    kernel_thread_t *decode_worker;
    kernel_thread_t *playback;
    /* Guards pcm_read/pcm_write/pcm_queued (the decode->playback ring
     * buffer) and workers/closing: decode_worker stays pinned to the boot
     * processor (it streams from the filesystem), but playback is not,
     * so irq_save() - which only ever stopped the calling core's own
     * interrupts - is no longer enough to keep the two from racing. */
    spinlock_t lock;
    int16_t *pcm;
    uint32_t pcm_read;
    uint32_t pcm_write;
    uint32_t pcm_queued;
    uint32_t workers;
    audio_decoder_t *loaded_decoder;
    const audio_codec_t *loaded_codec;
    audio_format_t loaded_format;
    uint64_t loaded_total;
    bool load_ok;
    volatile bool decode_done;
    bool eof;
    bool closing;
    bool playlist_open;
    bool seek_pending;
    bool seek_drag;
    bool volume_drag;
    bool seek_resume;
    uint64_t seek_frame;
    uint16_t visualizer_left[16];
    uint16_t visualizer_right[16];
    uint64_t visualizer_at;
} media_player_t;

static bool supported(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (kstrcasecmp(dot + 1, "wav") == 0 ||
                   kstrcasecmp(dot + 1, "ogg") == 0);
}

static void player_add(media_player_t *p, fs_node_t *node)
{
    if (p->count >= PLAYER_MAX_TRACKS || !node || node->type != FS_FILE ||
        !supported(node->name)) return;
    fs_path(node, p->tracks[p->count].path, PLAYER_PATH_MAX);
    strlcpy(p->tracks[p->count].name, node->name, FS_NAME_MAX);
    p->count++;
}

static void player_collect(media_player_t *p, fs_node_t *node)
{
    if (node->type == FS_FILE) { player_add(p, node); return; }
    for (fs_node_t *child = node->children; child; child = child->next)
        if (child->name[0] != '.') player_collect(p, child);
}

static void player_release_decoder(media_player_t *p)
{
    if (p->decoder) audio_decoder_close(p->decoder);
    kfree(p->pcm);
    p->pcm = NULL;
    p->pcm_read = p->pcm_write = p->pcm_queued = 0;
    p->decoder = NULL; p->codec = NULL; p->decoded = 0;
}

static void player_stop_decoder(media_player_t *p)
{
    audio_stream_stop();
    player_release_decoder(p);
}

static void player_destroy(media_player_t *p)
{
    if (p->loaded_decoder)
        audio_decoder_close(p->loaded_decoder);
    player_release_decoder(p);
    kfree(p);
}

static void player_worker_done(media_player_t *p)
{
    bool saved = spin_lock_irqsave(&p->lock);
    if (p->workers)
        p->workers--;
    bool destroy = p->closing && p->workers == 0;
    spin_unlock_irqrestore(&p->lock, saved);
    if (destroy)
        player_destroy(p);
}

static void player_load_worker(void *arg)
{
    media_player_t *p = arg;
    fs_node_t *node = NULL;
    if (fs_resolve(fs_root(), p->tracks[p->current].path, &node) == FS_OK &&
        node->type == FS_FILE && !thread_cancelled()) {
        p->loaded_decoder = audio_decoder_open_file(
            p->tracks[p->current].path, node, &p->loaded_format,
            &p->loaded_total, &p->loaded_codec);
        p->load_ok = p->loaded_decoder != NULL;
    }
    player_worker_done(p);
    thread_exit();
}

static void player_decode_worker(void *arg)
{
    media_player_t *p = arg;
    while (!thread_cancelled() && !p->closing && p->decoder) {
        uint32_t queued = p->pcm_queued;
        if (PLAYER_PCM_FRAMES - queued < PLAYER_DECODE_FRAMES) {
            thread_yield();
            continue;
        }
        size_t count = MIN((uint32_t)PLAYER_DECODE_FRAMES,
                           PLAYER_PCM_FRAMES - p->pcm_write);
        size_t n = audio_decoder_read(p->decoder,
                                      p->pcm + p->pcm_write * 2, count);
        if (!n)
            break;
        bool saved = spin_lock_irqsave(&p->lock);
        p->pcm_write = (p->pcm_write + (uint32_t)n) % PLAYER_PCM_FRAMES;
        p->pcm_queued += (uint32_t)n;
        spin_unlock_irqrestore(&p->lock, saved);
    }
    p->decode_done = true;
    player_worker_done(p);
    thread_exit();
}

static void player_playback_worker(void *arg)
{
    media_player_t *p = arg;
    bool prebuffered = false;
    while (!thread_cancelled() && !p->closing) {
        uint32_t queued = p->pcm_queued;
        if (!prebuffered) {
            if (queued < PLAYER_PREBUFFER_FRAMES && !p->decode_done) {
                thread_yield();
                continue;
            }
            prebuffered = true;
        }
        if (!queued) {
            if (p->decode_done)
                break;
            thread_yield();
            continue;
        }
        size_t count = MIN((uint32_t)PLAYER_DECODE_FRAMES, queued);
        count = MIN(count, PLAYER_PCM_FRAMES - p->pcm_read);
        if (audio_stream_queued_frames() + count >
            audio_stream_capacity_frames()) {
            audio_poll();
            thread_yield();
            continue;
        }
        size_t written = audio_stream_write(p->pcm + p->pcm_read * 2, count);
        if (!written)
            continue;
        bool saved = spin_lock_irqsave(&p->lock);
        p->pcm_read = (p->pcm_read + (uint32_t)written) % PLAYER_PCM_FRAMES;
        p->pcm_queued -= (uint32_t)written;
        spin_unlock_irqrestore(&p->lock, saved);
        p->decoded += written;
    }
    if (!thread_cancelled() && !p->closing && p->decode_done) {
        audio_stream_drain();
        while (audio_stream_queued_frames() && !thread_cancelled() &&
               !p->closing) {
            audio_poll();
            thread_yield();
        }
        p->eof = !thread_cancelled() && !p->closing;
    }
    player_worker_done(p);
    thread_exit();
}

static bool player_load(media_player_t *p, size_t index)
{
    if (index >= p->count || p->loader || p->decode_worker || p->playback)
        return false;
    fs_node_t *node = NULL;
    if (fs_resolve(fs_root(), p->tracks[index].path, &node) != FS_OK)
        return false;
    player_stop_decoder(p);
    p->current = index;
    p->loaded_decoder = NULL;
    p->loaded_codec = NULL;
    p->loaded_total = 0;
    p->load_ok = false;
    p->decode_done = false;
    p->eof = false;
    p->message[0] = '\0';
    p->state = PLAYER_LOADING;
    p->loader = thread_spawn_pinned("audio-load", player_load_worker, p);
    if (!p->loader) {
        strlcpy(p->message, "Unable to start audio loader", sizeof(p->message));
        p->state = PLAYER_ERROR;
        return false;
    }
    bool w_saved = spin_lock_irqsave(&p->lock);
    p->workers++;
    spin_unlock_irqrestore(&p->lock, w_saved);
    return true;
}

static void player_next(media_player_t *p)
{
    if (p->loader || p->decode_worker || p->playback)
        return;
    if (p->count && !player_load(p, (p->current + 1) % p->count))
        p->state = PLAYER_ERROR;
}

static void player_on_open(window_t *win, const char *arg)
{
    media_player_t *p = kcalloc(1, sizeof(*p));
    if (!p) return;
    p->hot = -1;
    p->state = PLAYER_STOPPED;
    p->playlist_open = true;
    win->priv = p;
    if (!arg || !arg[0]) return;
    fs_node_t *node = NULL;
    if (fs_resolve(fs_root(), arg, &node) != FS_OK) {
        strlcpy(p->message, "File not found", sizeof(p->message)); return;
    }
    player_collect(p, node);
    if (p->count) player_load(p, 0);
    else strlcpy(p->message, "No WAV or Ogg Vorbis files found", sizeof(p->message));
}

static void player_on_close(window_t *win)
{
    media_player_t *p = win->priv;
    win->priv = NULL;
    if (!p)
        return;

    bool saved = spin_lock_irqsave(&p->lock);
    p->workers++;
    p->closing = true;
    spin_unlock_irqrestore(&p->lock, saved);
    audio_stream_stop();
    kernel_thread_t **threads[] = {
        &p->loader, &p->decode_worker, &p->playback,
    };
    for (size_t i = 0; i < ARRAY_SIZE(threads); i++) {
        kernel_thread_t *thread = *threads[i];
        if (!thread)
            continue;
        if (thread_state(thread) == THREAD_DEAD)
            thread_join(thread);
        else {
            thread_cancel(thread);
            thread_detach(thread);
        }
        *threads[i] = NULL;
    }
    player_worker_done(p);
}

static const char *state_name(player_state_t state)
{
    return state == PLAYER_PLAYING ? "Playing" :
           state == PLAYER_LOADING ? "Loading" :
           state == PLAYER_PAUSED ? "Paused" :
           state == PLAYER_ERROR ? "Error" : "Stopped";
}

static void format_time(uint64_t frames, uint32_t rate, char *out, size_t size)
{
    uint32_t seconds = rate ? (uint32_t)(frames / rate) : 0;
    ksnprintf(out, size, "%u:%02u", seconds / 60, seconds % 60);
}

static void player_text_clip(rect_t clip, int x, int y, const char *text,
                             color_t color)
{
    gfx_clip_push(clip);
    gfx_text(x, y, text, color);
    gfx_clip_pop();
}

static void player_visualizer(const media_player_t *p, rect_t area)
{
    gfx_fill_rect(area.x, area.y, area.w, area.h, RGB(0x0d, 0x12, 0x1c));
    gfx_rect(area.x, area.y, area.w, area.h, RGB(0x3a, 0x4b, 0x68));
    const int bars = 24;
    int gap = 2;
    int bar_w = MAX(4, (area.w - (bars - 1) * gap) / bars);
    int block = MAX(4, MIN(9, bar_w));
    int step = block + 1;
    int max_blocks = MAX(1, (area.h - 8) / step);
    for (int bar = 0; bar < bars; bar++) {
        int x = area.x + bar * (bar_w + gap);
        int left = p ? p->visualizer_left[bar * 16 / bars] : 0;
        int right = p ? p->visualizer_right[bar * 16 / bars] : 0;
        int peak = (left + right) / 2;
        int blocks = peak * max_blocks / 32767;
        int t = bar * 255 / (bars - 1);
        color_t cold = RGB(0x35, 0xd0, 0xd0);
        color_t hot = RGB(0xe0, 0x67, 0xd3);
        int r = RGB_R(cold) + (RGB_R(hot) - RGB_R(cold)) * t / 255;
        int g = RGB_G(cold) + (RGB_G(hot) - RGB_G(cold)) * t / 255;
        int b = RGB_B(cold) + (RGB_B(hot) - RGB_B(cold)) * t / 255;
        for (int row = 0; row < blocks; row++) {
            int fade = 145 + (max_blocks - row) * 110 / max_blocks;
            color_t color = RGB(r * fade / 255, g * fade / 255,
                                b * fade / 255);
            int y = area.y + area.h - 4 - (row + 1) * step;
            gfx_fill_rect(x + (bar_w - block) / 2, y, block, block, color);
        }
    }
}

static void player_on_draw(window_t *win, rect_t client)
{
    media_player_t *p = win->priv;
    gfx_fill_rect(client.x, client.y, client.w, client.h, RGB(0x1a, 0x20, 0x2b));
    int progress_y = client.y + client.h - 106;
    int bottom_y = client.y + client.h - 62;
    int top_x = client.x + 12;
    int top_w = client.w - 24;
    if (p && p->playlist_open) {
        int list_w = 184;
        gfx_fill_rect(top_x, client.y + 12, list_w, progress_y - client.y - 48,
                      RGB(0x24, 0x2b, 0x38));
        gfx_rect(top_x, client.y + 12, list_w, progress_y - client.y - 48,
                 RGB(0x4a, 0x59, 0x73));
        gfx_text(top_x + 12, client.y + 24, "Playlist",
                 RGB(0xb5, 0xc4, 0xd8));
        for (size_t i = 0; i < p->count && i < 10; i++) {
            int y = client.y + 48 + (int)i * 22;
            if (i == p->current)
                gfx_fill_rect(top_x + 4, y - 3, list_w - 8, 20,
                              COLOR_SELECTION);
            player_text_clip(rect_make(top_x + 12, y - 3, list_w - 24, 20),
                             top_x + 12, y, p->tracks[i].name,
                             i == p->current ? RGB(0xff, 0xff, 0xff) :
                                               RGB(0xd1, 0xd7, 0xe2));
        }
        top_x += list_w + 12;
        top_w -= list_w + 12;
    }
    player_visualizer(p, rect_make(top_x, client.y + 12, top_w,
                                   progress_y - client.y - 48));

    /* A conventional transport strip: timestamps belong to the progress
     * bar, while transport, volume and track metadata stay below it. */
    if (p) {
        char elapsed[16], total[16];
        format_time(p->decoded, p->format.sample_rate, elapsed, sizeof(elapsed));
        format_time(p->total, p->format.sample_rate, total, sizeof(total));
        const char *name = p->count ? p->tracks[p->current].name : "No track";
        player_text_clip(rect_make(client.x + 64, progress_y - 27,
                                   client.w - 128, 16), client.x + 64,
                         progress_y - 27, name, RGB(0xe5, 0xeb, 0xf5));
        gfx_text(client.x + 16, progress_y - 11, elapsed, COLOR_TEXT_DIM);
        player_text_clip(rect_make(client.x + client.w - 56, progress_y - 11,
                                   40, 16), client.x + client.w - 56,
                         progress_y - 11, total, COLOR_TEXT_DIM);
        widget_slider(rect_make(client.x + 16, progress_y, client.w - 32, 14),
                      p->total ? (int)MIN(p->decoded, p->total) : 0,
                      p->total ? (int)MIN(p->total, 0x7fffffff) : 1);
        if (p->message[0])
            player_text_clip(rect_make(client.x + 220, client.y + 6,
                                       client.w - 232, 18), client.x + 220,
                             client.y + 11, p->message, COLOR_DANGER);
    }

    widget_button_icon(rect_make(client.x + 16, bottom_y, 34, 34), ICON_FOLDER,
                       "", false, false);

    int transport_x = client.x + client.w / 2 - 70;
    widget_button_icon(rect_make(transport_x, bottom_y, 34, 34), ICON_BACK,
                       "", false, false);
    widget_button_icon(rect_make(transport_x + 40, bottom_y, 42, 34),
                       p && p->state == PLAYER_PLAYING ? ICON_PAUSE : ICON_PLAY,
                       "", false, false);
    widget_button_icon(rect_make(transport_x + 88, bottom_y, 34, 34), ICON_POWER,
                       "", false, false);
    widget_button_icon(rect_make(transport_x + 128, bottom_y, 34, 34),
                       ICON_FORWARD, "", false, false);

    widget_icon(client.x + client.w - 126, bottom_y + 9, 16, ICON_VOLUME);
    widget_slider(rect_make(client.x + client.w - 104, bottom_y + 9, 88, 16),
                  audio_get_volume(), 100);

    if (p) {
        char meta[96];
        const char *name = p->count ? p->tracks[p->current].name : "No track";
        ksnprintf(meta, sizeof(meta), "%s  ·  %s", name,
                  state_name(p->state));
        player_text_clip(rect_make(client.x + 430, bottom_y + 4, 82, 28),
                         client.x + 430, bottom_y + 8, meta,
                         RGB(0xd1, 0xd7, 0xe2));
    }
}

static bool player_on_key(window_t *win, const key_event_t *ev)
{
    media_player_t *p = win->priv;
    if (!p || ev->code != ' ') return false;
    if (p->state == PLAYER_PLAYING) { audio_stream_pause(true); p->state = PLAYER_PAUSED; }
    else if (p->state == PLAYER_PAUSED) { audio_stream_pause(false); p->state = PLAYER_PLAYING; }
    wm_invalidate(win); return true;
}

static bool player_on_mouse(window_t *win, const mouse_event_t *ev, int x, int y)
{
    media_player_t *p = win->priv;
    if (!p) return false;
    rect_t client = wm_client_rect(win);
    int bottom_y = client.h - 62;
    int transport_x = client.w / 2 - 70;

    if (p->volume_drag && (ev->buttons & MOUSE_LEFT || ev->released & MOUSE_LEFT)) {
        audio_set_volume((uint8_t)widget_slider_value(
            rect_make(client.w - 104, bottom_y + 9, 88, 16), x, 100));
        if (ev->released & MOUSE_LEFT)
            p->volume_drag = false;
        wm_invalidate(win);
        return true;
    }
    if (p->seek_drag && (ev->buttons & MOUSE_LEFT || ev->released & MOUSE_LEFT)) {
        p->seek_frame = (uint64_t)widget_slider_value(
            rect_make(16, client.h - 106, client.w - 32, 14), x, 1000000);
        p->seek_frame = p->total ? p->seek_frame * p->total / 1000000 : 0;
        if (ev->released & MOUSE_LEFT) {
            p->seek_drag = false;
            p->seek_pending = true;
        }
        wm_invalidate(win);
        return true;
    }
    if (!(ev->pressed & MOUSE_LEFT))
        return false;
    if (y >= bottom_y && y < bottom_y + 38) {
        if (x >= client.w - 104 && x < client.w - 16) {
            p->volume_drag = true;
            audio_set_volume((uint8_t)widget_slider_value(
                rect_make(client.w - 104, bottom_y + 9, 88, 16), x, 100));
        } else if (x >= 16 && x < 50) {
            p->playlist_open = !p->playlist_open;
        } else if (x >= transport_x && x < transport_x + 34 && p->count) {
            player_load(p, (p->current + p->count - 1) % p->count);
        } else if (x >= transport_x + 40 && x < transport_x + 82) {
            if (p->state == PLAYER_PLAYING) { audio_stream_pause(true); p->state = PLAYER_PAUSED; }
            else if (p->state == PLAYER_PAUSED) { audio_stream_pause(false); p->state = PLAYER_PLAYING; }
        } else if (x >= transport_x + 88 && x < transport_x + 122) {
            audio_stream_stop();
            p->state = PLAYER_STOPPED;
            if (p->decode_worker) thread_cancel(p->decode_worker);
            if (p->playback) thread_cancel(p->playback);
            if (!p->decode_worker && !p->playback)
                player_stop_decoder(p);
        }
        else if (x >= transport_x + 128 && x < transport_x + 162 && p->count)
            player_next(p);
        wm_invalidate(win); return true;
    }
    int progress_y = client.h - 106;
    if (y >= progress_y - 4 && y < progress_y + 18 && p->total && p->decoder &&
        p->codec && p->codec->seek) {
        p->seek_drag = true;
        p->seek_frame = (uint64_t)widget_slider_value(
            rect_make(16, progress_y, client.w - 32, 14), x, 1000000);
        p->seek_frame = p->seek_frame * p->total / 1000000;
        p->seek_resume = p->state == PLAYER_PLAYING;
        p->seek_pending = false;
        audio_stream_stop();
        if (p->decode_worker) thread_cancel(p->decode_worker);
        if (p->playback) thread_cancel(p->playback);
        p->state = PLAYER_PAUSED;
        wm_invalidate(win);
        return true;
    }
    if (p->playlist_open && x >= 12 && x < 196 && y >= 48 && y < 268 && p->count) {
        size_t index = (size_t)((y - 48) / 22);
        if (index < p->count) player_load(p, index);
        wm_invalidate(win); return true;
    }
    return false;
}

static bool player_on_tick(window_t *win)
{
    media_player_t *p = win->priv;
    uint64_t now = timer_ticks();
    bool redraw = false;
    if (p && now >= p->visualizer_at) {
        p->visualizer_at = now + 80;
        redraw = true;
        uint32_t queued = p->pcm_queued;
        for (int bar = 0; bar < 16; bar++) {
            uint32_t begin = queued * (uint32_t)bar / 16;
            uint32_t end = queued * (uint32_t)(bar + 1) / 16;
            int peak_left = 0, peak_right = 0;
            if (p->pcm && end > begin) {
                for (uint32_t i = begin; i < end; i += 64) {
                    int left = p->pcm[((p->pcm_read + i) % PLAYER_PCM_FRAMES) * 2];
                    int right = p->pcm[((p->pcm_read + i) % PLAYER_PCM_FRAMES) * 2 + 1];
                    peak_left = MAX(peak_left, left < 0 ? -left : left);
                    peak_right = MAX(peak_right, right < 0 ? -right : right);
                }
            }
            uint16_t targets[2] = {
                (uint16_t)MIN(32767, peak_left),
                (uint16_t)MIN(32767, peak_right),
            };
            uint16_t *levels[2] = {
                &p->visualizer_left[bar], &p->visualizer_right[bar],
            };
            for (int channel = 0; channel < 2; channel++) {
                uint16_t target = targets[channel];
                uint16_t current = *levels[channel];
                if (target > current)
                    current = (uint16_t)MIN(target,
                        current + (target - current) / 3 + 1);
                else if (current > target)
                    current = (uint16_t)(current - MIN(current - target,
                        (current - target) / 5 + 1));
                *levels[channel] = current;
            }
        }
    }
    if (p && p->decode_worker &&
        thread_state(p->decode_worker) == THREAD_DEAD) {
        thread_join(p->decode_worker);
        p->decode_worker = NULL;
    }
    if (p && p->playback && thread_state(p->playback) == THREAD_DEAD) {
        thread_join(p->playback);
        p->playback = NULL;
    }
    if (p && !p->decode_worker && !p->playback && p->decoder) {
        if (p->seek_pending && !p->seek_drag) {
            p->seek_pending = false;
            if (!audio_decoder_seek(p->decoder, p->seek_frame)) {
                strlcpy(p->message, "This audio format cannot seek",
                        sizeof(p->message));
                p->state = PLAYER_ERROR;
            } else {
                p->decoded = p->seek_frame;
                p->eof = false;
                p->decode_done = false;
                p->pcm_read = p->pcm_write = p->pcm_queued = 0;
                if (!audio_stream_start(&p->format)) {
                    strlcpy(p->message, "Audio output is unavailable",
                            sizeof(p->message));
                    p->state = PLAYER_ERROR;
                } else {
                    p->decode_worker = thread_spawn_pinned("audio-decode",
                                                    player_decode_worker, p);
                    p->playback = p->decode_worker
                        ? thread_spawn("audio-play", player_playback_worker, p)
                        : NULL;
                    if (!p->decode_worker || !p->playback) {
                        if (p->decode_worker) thread_cancel(p->decode_worker);
                        if (p->playback) thread_cancel(p->playback);
                        audio_stream_stop();
                        p->state = PLAYER_ERROR;
                    } else {
                        bool w_saved = spin_lock_irqsave(&p->lock);
                        p->workers += 2;
                        spin_unlock_irqrestore(&p->lock, w_saved);
                        p->state = p->seek_resume ? PLAYER_PLAYING : PLAYER_PAUSED;
                        if (!p->seek_resume)
                            audio_stream_pause(true);
                    }
                }
            }
        } else if (p->eof && p->state == PLAYER_PLAYING) {
            player_stop_decoder(p);
            player_next(p);
        } else if (p->state == PLAYER_STOPPED) {
            player_stop_decoder(p);
        } else if (p->state == PLAYER_ERROR) {
            player_stop_decoder(p);
        }
        wm_invalidate(win);
    }
    if (p && p->loader && p->state != PLAYER_LOADING) {
        if (thread_state(p->loader) == THREAD_DEAD) {
            thread_join(p->loader);
            p->loader = NULL;
            if (p->loaded_decoder)
                audio_decoder_close(p->loaded_decoder);
            p->loaded_decoder = NULL;
            return true;
        }
        return false;
    }
    if (p && p->state == PLAYER_LOADING) {
        if (!p->loader || thread_state(p->loader) != THREAD_DEAD)
            return true;
        thread_join(p->loader);
        p->loader = NULL;
        if (!p->load_ok) {
            strlcpy(p->message, "Unsupported or malformed audio file",
                    sizeof(p->message));
            p->state = PLAYER_ERROR;
            return true;
        }
        p->decoder = p->loaded_decoder;
        p->codec = p->loaded_codec;
        p->format = p->loaded_format;
        p->total = p->loaded_total;
        p->loaded_decoder = NULL;
        p->loaded_codec = NULL;
        p->pcm = kmalloc(PLAYER_PCM_FRAMES * 2u * sizeof(*p->pcm));
        if (!p->pcm) {
            strlcpy(p->message, "Unable to allocate audio buffer",
                    sizeof(p->message));
            player_stop_decoder(p);
            p->state = PLAYER_ERROR;
            return true;
        }
        p->pcm_read = p->pcm_write = p->pcm_queued = 0;
        p->decode_done = false;
        if (!audio_stream_start(&p->format)) {
            strlcpy(p->message, "AC'97 audio output is unavailable",
                    sizeof(p->message));
            audio_decoder_close(p->decoder);
            p->decoder = NULL;
            p->state = PLAYER_ERROR;
            return true;
        }
        p->decode_worker = thread_spawn_pinned("audio-decode",
                                        player_decode_worker, p);
        if (!p->decode_worker) {
            strlcpy(p->message, "Unable to start audio decoder",
                    sizeof(p->message));
            player_stop_decoder(p);
            p->state = PLAYER_ERROR;
            return true;
        }
        bool w_saved = spin_lock_irqsave(&p->lock);
        p->workers++;
        spin_unlock_irqrestore(&p->lock, w_saved);
        p->playback = thread_spawn("audio-play", player_playback_worker, p);
        if (!p->playback) {
            strlcpy(p->message, "Unable to start audio playback",
                    sizeof(p->message));
            thread_cancel(p->decode_worker);
            audio_stream_stop();
            p->state = PLAYER_ERROR;
            return true;
        }
        w_saved = spin_lock_irqsave(&p->lock);
        p->workers++;
        spin_unlock_irqrestore(&p->lock, w_saved);
        p->state = PLAYER_PLAYING;
        return true;
    }
    if (!p || p->state != PLAYER_PLAYING || !p->decoder) return false;
    return redraw;
}

static const app_class_t media_player_class = {
    .name = "mplayer", .on_open = player_on_open, .on_close = player_on_close,
    .on_draw = player_on_draw, .on_key = player_on_key,
    .on_mouse = player_on_mouse, .on_tick = player_on_tick,
};

window_t *app_media_player_open(const char *path)
{ return wm_open(&media_player_class, 640, 430, "Media Player", path); }
