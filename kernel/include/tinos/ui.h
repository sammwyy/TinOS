/* Window manager, widgets and the application registry. */
#ifndef TINOS_UI_H
#define TINOS_UI_H

#include <tinos/gfx.h>
#include <tinos/input.h>
#include <tinos/kernel.h>
#include <tinos/types.h>

/* ------------------------------------------------------------------ */
/* Theme                                                               */
/* ------------------------------------------------------------------ */

#define COLOR_DESKTOP_TOP     RGB(0x3a, 0x49, 0x61)
#define COLOR_DESKTOP_BOTTOM  RGB(0x18, 0x20, 0x2d)
#define COLOR_TASKBAR         RGB(0xb8, 0xb7, 0xc3)
#define COLOR_TASKBAR_EDGE    RGB(0x4a, 0x49, 0x59)
#define COLOR_ACCENT          RGB(0x5f, 0x6e, 0x99)
#define COLOR_ACCENT_DARK     RGB(0x3e, 0x4d, 0x73)
#define COLOR_PANEL           RGB(0xc2, 0xc1, 0xcc)
#define COLOR_PANEL_LIGHT     RGB(0xe3, 0xe1, 0xe8)
#define COLOR_BORDER          RGB(0x5c, 0x5b, 0x6c)
#define COLOR_TEXT            RGB(0x16, 0x1b, 0x24)
#define COLOR_TEXT_DIM        RGB(0x4f, 0x5a, 0x69)
#define COLOR_TEXT_DARK       RGB(0x10, 0x14, 0x1c)
#define COLOR_WINDOW_BG       RGB(0xd0, 0xcf, 0xda)
#define COLOR_TITLE_ACTIVE    RGB(0x5e, 0x6d, 0x98)
#define COLOR_TITLE_INACTIVE  RGB(0x8c, 0x8c, 0x9b)
#define COLOR_TERMINAL_BG     RGB(0x0d, 0x11, 0x18)
#define COLOR_TERMINAL_TEXT   RGB(0xd6, 0xe2, 0xf0)
#define COLOR_TERMINAL_PROMPT RGB(0x6e, 0xe7, 0xb7)
#define COLOR_TERMINAL_ERROR  RGB(0xff, 0x8a, 0x8a)
#define COLOR_SELECTION       RGB(0x5b, 0x80, 0xa8)
#define COLOR_FOLDER          RGB(0xf2, 0xc0, 0x54)
#define COLOR_FILE            RGB(0xd6, 0xdd, 0xe8)
#define COLOR_DANGER          RGB(0xe0, 0x5c, 0x5c)
#define COLOR_OK              RGB(0x5c, 0xd0, 0x8a)

#define TASKBAR_HEIGHT 28
#define TITLEBAR_HEIGHT 20
#define WINDOW_BORDER 2
#define RESIZE_GRIP 12
#define WINDOW_MIN_W 180
#define WINDOW_MIN_H 120

/* ------------------------------------------------------------------ */
/* Windows                                                             */
/* ------------------------------------------------------------------ */

typedef struct window window_t;

typedef struct {
    const char *name;
    void (*on_open)(window_t *w, const char *arg);
    void (*on_close)(window_t *w);
    void (*on_draw)(window_t *w, rect_t client);
    /* Return true when the event was consumed. */
    bool (*on_key)(window_t *w, const key_event_t *ev);
    bool (*on_mouse)(window_t *w, const mouse_event_t *ev, int lx, int ly);
    /* Called once per event loop iteration; return true to request a redraw. */
    bool (*on_tick)(window_t *w);
    /* Called after the frame changes size, so the app can recompute derived
     * layout (columns/rows, wrap width). Optional: most apps derive their
     * layout from the client rect on every draw and don't need this. */
    void (*on_resize)(window_t *w, rect_t client);
} app_class_t;

struct window {
    int                 id;
    char                title[64];
    rect_t              frame;
    rect_t              restore_frame;
    bool                maximized;
    bool                minimized;
    bool                needs_redraw;
    uint32_t           *surface;
    size_t              surface_words;
    bool                surface_valid;
    const app_class_t  *cls;
    void               *priv;
    window_t           *below; /* z-order: NULL for the bottom window */
    window_t           *above; /* NULL for the top window             */
};

void      wm_init(void);
window_t *wm_open(const app_class_t *cls, int width, int height,
                  const char *title, const char *arg);
void      wm_close(window_t *win);
void      wm_focus(window_t *win);
window_t *wm_focused(void);
void      wm_invalidate(window_t *win);
void      wm_invalidate_all(void);
bool      wm_any_dirty(void);
/* True when the whole desktop must be repainted (opening a window, focus or
 * z-order changes). Clears the flag. */
bool      wm_take_full_redraw(void);
/* Rectangles a moved/closed/minimised window uncovered since the last call;
 * the wallpaper (and anything else under them) needs repainting there.
 * Returns the number written to `out` (capped at `max`) and clears them. */
size_t    wm_take_exposed(rect_t *out, size_t max);
void      wm_draw_window(window_t *win); /* frame + client contents */
bool      wm_window_cached(const window_t *win);
void      wm_blit_window(window_t *win);
rect_t    wm_client_rect(const window_t *win);
void      wm_set_title(window_t *win, const char *title);
void      wm_cycle_focus(void);
void      wm_toggle_minimize(window_t *win);

/* Iteration in z-order, bottom first. */
window_t *wm_bottom(void);
window_t *wm_top(void);
size_t    wm_count(void);

/* Event routing (called by the desktop loop). Returns true when consumed. */
bool wm_handle_key(const key_event_t *ev);
bool wm_handle_mouse(const mouse_event_t *ev);

/* The area windows may occupy (the screen minus the taskbar). */
rect_t wm_workspace(void);

/* ------------------------------------------------------------------ */
/* Widgets                                                             */
/* ------------------------------------------------------------------ */

typedef enum {
    ICON_TERMINAL = 0,
    ICON_FOLDER,
    ICON_FILE,
    ICON_INFO,
    ICON_CHIP,
    ICON_POWER,
    ICON_RESTART,
    ICON_EDITOR,
    ICON_TINOS,
    ICON_BACK,
    ICON_FORWARD,
    ICON_UP,
    ICON_HOME,
    ICON_DRIVE,
    ICON_REFRESH,
    ICON_SAVE,
    ICON_NEW,
    ICON_UNDO,
    ICON_REDO,
} icon_t;

void widget_icon(int x, int y, int size, icon_t icon);
/* Draw a build-time resource icon by its BMP filename stem (e.g. "test").
 * Returns false when no such resource exists. */
bool widget_icon_name(int x, int y, int size, const char *name);
void widget_button(rect_t r, const char *label, bool hot, bool pressed);
void widget_panel(rect_t r, color_t fill, color_t border);
void widget_text_centered(rect_t r, const char *text, color_t color);
void widget_input_frame(rect_t r);
void widget_scrollbar(rect_t track, size_t first_visible, size_t visible,
                      size_t total);

/* A single line text field used by the explorer prompts and the editor. */
#define TEXT_INPUT_MAX 128

typedef struct {
    char   buffer[TEXT_INPUT_MAX];
    size_t length;
    size_t cursor;
    bool   active;
    /* The pre-filled text starts out "selected": the first character typed
     * replaces it, the way a rename box behaves elsewhere. */
    bool   select_all;
} text_input_t;

void text_input_reset(text_input_t *input, const char *initial);
bool text_input_key(text_input_t *input, const key_event_t *ev);
void text_input_draw(const text_input_t *input, rect_t r, const char *label,
                     bool blink_on);

/* ------------------------------------------------------------------ */
/* Applications                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;
    const char *summary;
    const char *category; /* start menu grouping, e.g. "Accessories" */
    icon_t      icon;
    window_t *(*launch)(const char *arg);
} app_entry_t;

const app_entry_t *apps_registry(size_t *count);

window_t *app_terminal_open(const char *cwd);
window_t *app_explorer_open(const char *path);
window_t *app_editor_open(const char *path);
window_t *app_about_open(const char *arg);
window_t *app_sysinfo_open(const char *arg);

/* Used by applications to raise a transient message on the desktop. */
void desktop_notify(const char *fmt, ...) PRINTFLIKE(1, 2);

#endif /* TINOS_UI_H */
