#include <tinos/audio.h>
#include <tinos/codec.h>
#include <tinos/fs.h>
#include <tinos/gfx.h>
#include <tinos/heap.h>
#include <tinos/kernel.h>
#include <tinos/printf.h>
#include <tinos/string.h>
#include <tinos/thread.h>
#include <tinos/ui.h>

#define PLAYER_MAX_TRACKS 64
#define PLAYER_PATH_MAX  FS_PATH_MAX
#define PLAYER_H 30

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
    kernel_thread_t *playback;
    audio_decoder_t *loaded_decoder;
    const audio_codec_t *loaded_codec;
    audio_format_t loaded_format;
    uint64_t loaded_total;
    bool load_ok;
    bool eof;
    bool closing;
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

static void player_stop_decoder(media_player_t *p)
{
    audio_stream_stop();
    if (p->decoder) audio_decoder_close(p->decoder);
    p->decoder = NULL; p->codec = NULL; p->decoded = 0;
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
    if (p->closing) {
        if (p->loaded_decoder)
            audio_decoder_close(p->loaded_decoder);
        kfree(p);
    }
    thread_exit();
}

static void player_playback_worker(void *arg)
{
    media_player_t *p = arg;
    int16_t buffer[512 * 2];
    while (!thread_cancelled() && !p->closing && p->decoder) {
        if (audio_stream_queued_frames() > 6144) {
            audio_poll();
            thread_yield();
            continue;
        }
        size_t n = audio_decoder_read(p->decoder, buffer, 512);
        if (!n) { p->eof = true; break; }
        size_t written = audio_stream_write(buffer, n);
        p->decoded += written;
        if (!written) {
            audio_poll();
            thread_yield();
        }
    }
    if (p->closing) {
        if (p->decoder) audio_decoder_close(p->decoder);
        audio_stream_stop();
        kfree(p);
    }
    thread_exit();
}

static bool player_load(media_player_t *p, size_t index)
{
    if (index >= p->count || p->loader || p->playback) return false;
    if (p->loader) return false;
    fs_node_t *node = NULL;
    if (fs_resolve(fs_root(), p->tracks[index].path, &node) != FS_OK)
        return false;
    player_stop_decoder(p);
    p->current = index;
    p->loaded_decoder = NULL;
    p->loaded_codec = NULL;
    p->loaded_total = 0;
    p->load_ok = false;
    p->eof = false;
    p->message[0] = '\0';
    p->state = PLAYER_LOADING;
    p->loader = thread_spawn("audio-load", player_load_worker, p);
    if (!p->loader) {
        strlcpy(p->message, "Unable to start audio loader", sizeof(p->message));
        p->state = PLAYER_ERROR;
        return false;
    }
    return true;
}

static void player_next(media_player_t *p)
{
    if (p->loader || p->playback)
        return;
    if (p->count && !player_load(p, (p->current + 1) % p->count))
        p->state = PLAYER_ERROR;
}

static void player_on_open(window_t *win, const char *arg)
{
    media_player_t *p = kcalloc(1, sizeof(*p));
    if (!p) return;
    p->hot = -1; p->state = PLAYER_STOPPED; win->priv = p;
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
    if (p) {
        if (p->loader) {
            if (thread_state(p->loader) == THREAD_DEAD) {
                thread_join(p->loader);
                p->loader = NULL;
                if (p->loaded_decoder)
                    audio_decoder_close(p->loaded_decoder);
                player_stop_decoder(p);
                kfree(p);
                win->priv = NULL;
                return;
            } else {
                p->closing = true;
                thread_cancel(p->loader);
                thread_detach(p->loader);
                audio_stream_stop();
                win->priv = NULL;
                return;
            }
        }
        if (p->playback) {
            if (thread_state(p->playback) == THREAD_DEAD) {
                thread_join(p->playback);
                p->playback = NULL;
                player_stop_decoder(p);
                kfree(p);
                win->priv = NULL;
                return;
            }
            p->closing = true;
            thread_cancel(p->playback);
            thread_detach(p->playback);
            audio_stream_stop();
            win->priv = NULL;
            return;
        }
        if (p->loaded_decoder)
            audio_decoder_close(p->loaded_decoder);
        player_stop_decoder(p);
        kfree(p);
    }
    win->priv = NULL;
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

static void player_on_draw(window_t *win, rect_t client)
{
    media_player_t *p = win->priv;
    gfx_fill_rect(client.x, client.y, client.w, client.h, COLOR_WINDOW_BG);
    gfx_fill_rect(client.x, client.y, client.w, 38, COLOR_ACCENT_DARK);
    gfx_text(client.x + 16, client.y + 11, "Media Player", RGB(255,255,255));
    widget_button(rect_make(client.x + client.w - 82, client.y + 6, 68, 26),
                  "Open", false, false);
    int list_x = client.x + 12, list_y = client.y + 50;
    gfx_text(list_x, list_y, "Playlist", COLOR_TEXT_DIM);
    for (size_t i = 0; p && i < p->count && i < 10; i++) {
        int y = list_y + 22 + (int)i * 20;
        if (i == p->current) gfx_fill_rect(list_x - 4, y - 2, client.w - 24, 19,
                                           COLOR_SELECTION);
        gfx_text(list_x, y, p->tracks[i].name,
                 i == p->current ? RGB(255,255,255) : COLOR_TEXT);
    }
    int controls = client.y + client.h - 72;
    widget_button(rect_make(client.x + 12, controls, 60, 26), "Prev", false, false);
    widget_button(rect_make(client.x + 78, controls, 70, 26),
                  p && p->state == PLAYER_PLAYING ? "Pause" : "Play", false, false);
    widget_button(rect_make(client.x + 154, controls, 60, 26), "Stop", false, false);
    widget_button(rect_make(client.x + 220, controls, 60, 26), "Next", false, false);
    if (p) {
        char status[128];
        char elapsed[16], total[16];
        const char *name = p->count ? p->tracks[p->current].name : "No track selected";
        format_time(p->decoded, p->format.sample_rate, elapsed, sizeof(elapsed));
        format_time(p->total, p->format.sample_rate, total, sizeof(total));
        ksnprintf(status, sizeof(status), "%s  |  %s  |  %s", name,
                  p->codec ? p->codec->name : "-", state_name(p->state));
        gfx_text(client.x + 12, controls - 42, status, COLOR_TEXT);
        gfx_rect(client.x + 12, controls - 18, client.w - 24, 8, COLOR_BORDER);
        if (p->total) gfx_fill_rect(client.x + 12, controls - 18,
                                   (client.w - 24) * p->decoded / p->total, 8,
                                   COLOR_ACCENT);
        char timing[40];
        ksnprintf(timing, sizeof(timing), "%s / %s", elapsed, total);
        gfx_text(client.x + client.w - 92, controls - 42, timing, COLOR_TEXT_DIM);
        if (p->message[0]) gfx_text(client.x + 12, client.y + 28, p->message,
                                    COLOR_DANGER);
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
    if (!p || !(ev->pressed & MOUSE_LEFT)) return false;
    rect_t client = wm_client_rect(win);
    if (y < 38 && x >= client.w - 88) {
        app_explorer_open(TINOS_DEFAULT_HOME);
        return true;
    }
    int controls = wm_client_rect(win).h - 72;
    if (y >= controls && y < controls + 30) {
        if (x < 72 && p->count) player_load(p, (p->current + p->count - 1) % p->count);
        else if (x < 154) {
            if (p->state == PLAYER_PLAYING) { audio_stream_pause(true); p->state = PLAYER_PAUSED; }
            else if (p->state == PLAYER_PAUSED) { audio_stream_pause(false); p->state = PLAYER_PLAYING; }
        } else if (x < 220) {
            audio_stream_stop();
            p->state = PLAYER_STOPPED;
            if (!p->playback) player_stop_decoder(p);
        }
        else if (p->count) player_next(p);
        wm_invalidate(win); return true;
    }
    if (y >= 70 && y < 270 && p->count) {
        size_t index = (size_t)((y - 72) / 20);
        if (index < p->count) player_load(p, index);
        wm_invalidate(win); return true;
    }
    return false;
}

static bool player_on_tick(window_t *win)
{
    media_player_t *p = win->priv;
    if (p && p->playback && thread_state(p->playback) == THREAD_DEAD) {
        thread_join(p->playback);
        p->playback = NULL;
        if (p->eof && p->state == PLAYER_PLAYING) {
            player_stop_decoder(p);
            player_next(p);
        } else if (p->state == PLAYER_STOPPED) {
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
        if (!audio_stream_start(&p->format)) {
            strlcpy(p->message, "AC'97 audio output is unavailable",
                    sizeof(p->message));
            audio_decoder_close(p->decoder);
            p->decoder = NULL;
            p->state = PLAYER_ERROR;
            return true;
        }
        p->state = PLAYER_PLAYING;
        p->playback = thread_spawn("audio-play", player_playback_worker, p);
        if (!p->playback) {
            strlcpy(p->message, "Unable to start audio playback",
                    sizeof(p->message));
            player_stop_decoder(p);
            p->state = PLAYER_ERROR;
        }
        return true;
    }
    if (!p || p->state != PLAYER_PLAYING || !p->decoder) return false;
    return true;
}

static const app_class_t media_player_class = {
    .name = "mplayer", .on_open = player_on_open, .on_close = player_on_close,
    .on_draw = player_on_draw, .on_key = player_on_key,
    .on_mouse = player_on_mouse, .on_tick = player_on_tick,
};

window_t *app_media_player_open(const char *path)
{ return wm_open(&media_player_class, 560, 390, "Media Player", path); }
