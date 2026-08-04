/* Window manager.
 *
 * Windows are kept in a doubly linked z-order list (bottom first).  The
 * manager owns the frame - border, title bar, minimise and close buttons,
 * dragging - and delegates the client area to the application class.
 */

#include <tinos/gfx.h>
#include <tinos/heap.h>
#include <tinos/kernel.h>
#include <tinos/string.h>
#include <tinos/ui.h>

#define BUTTON_SIZE 16
#define BUTTON_GAP  6

static window_t *bottom_window;
static window_t *top_window;
static size_t    window_count;
static int       next_id = 1;
static bool      any_dirty;
static bool      full_redraw;

/* Rectangles uncovered by a moved/closed/minimised window, repainted from
 * the wallpaper up instead of the whole desktop. */
#define WM_MAX_EXPOSED 16
static rect_t exposed_rects[WM_MAX_EXPOSED];
static size_t exposed_count;

/* Drag state */
static window_t *dragging;
static int       drag_offset_x, drag_offset_y;

/* Resize state: dragging the bottom-right grip. */
static window_t *resizing;
static int       resize_start_w, resize_start_h;
static int       resize_start_x, resize_start_y; /* mouse position at press */

rect_t wm_workspace(void)
{
    return rect_make(0, 0, gfx_width(), gfx_height() - TASKBAR_HEIGHT);
}

window_t *wm_bottom(void)
{
    return bottom_window;
}

window_t *wm_top(void)
{
    return top_window;
}

size_t wm_count(void)
{
    return window_count;
}

void wm_init(void)
{
    bottom_window = top_window = NULL;
    window_count              = 0;
    next_id                   = 1;
    any_dirty                 = true;
}

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */

rect_t wm_client_rect(const window_t *win)
{
    return rect_make(win->frame.x + WINDOW_BORDER,
                     win->frame.y + TITLEBAR_HEIGHT,
                     win->frame.w - 2 * WINDOW_BORDER,
                     win->frame.h - TITLEBAR_HEIGHT - WINDOW_BORDER);
}

static rect_t close_button_rect(const window_t *win)
{
    return rect_make(win->frame.x + win->frame.w - BUTTON_SIZE - BUTTON_GAP,
                     win->frame.y + (TITLEBAR_HEIGHT - BUTTON_SIZE) / 2,
                     BUTTON_SIZE, BUTTON_SIZE);
}

static rect_t minimize_button_rect(const window_t *win)
{
    rect_t close = close_button_rect(win);
    return rect_make(close.x - BUTTON_SIZE - BUTTON_GAP, close.y, BUTTON_SIZE,
                     BUTTON_SIZE);
}

static rect_t resize_grip_rect(const window_t *win)
{
    return rect_make(win->frame.x + win->frame.w - RESIZE_GRIP,
                     win->frame.y + win->frame.h - RESIZE_GRIP, RESIZE_GRIP,
                     RESIZE_GRIP);
}

static rect_t titlebar_rect(const window_t *win)
{
    return rect_make(win->frame.x, win->frame.y, win->frame.w,
                     TITLEBAR_HEIGHT);
}

/* ------------------------------------------------------------------ */
/* List handling                                                       */
/* ------------------------------------------------------------------ */

static void unlink_window(window_t *win)
{
    if (win->below)
        win->below->above = win->above;
    else
        bottom_window = win->above;
    if (win->above)
        win->above->below = win->below;
    else
        top_window = win->below;
    win->above = win->below = NULL;
}

static void push_to_top(window_t *win)
{
    win->below = top_window;
    win->above = NULL;
    if (top_window)
        top_window->above = win;
    top_window = win;
    if (!bottom_window)
        bottom_window = win;
}

void wm_invalidate(window_t *win)
{
    if (!win)
        return;
    win->needs_redraw = true;
    any_dirty         = true;
}

void wm_invalidate_all(void)
{
    for (window_t *w = bottom_window; w; w = w->above)
        w->needs_redraw = true;
    any_dirty   = true;
    full_redraw = true;
}

bool wm_any_dirty(void)
{
    bool dirty = any_dirty;
    any_dirty  = false;
    for (window_t *w = bottom_window; w; w = w->above)
        if (w->needs_redraw)
            dirty = true;
    return dirty;
}

bool wm_take_full_redraw(void)
{
    bool full   = full_redraw;
    full_redraw = false;
    return full;
}

static bool rects_overlap(rect_t a, rect_t b)
{
    return !rect_empty(rect_intersect(a, b));
}

/* Mark every window overlapping `r` dirty, without forcing a full repaint. */
static void wm_invalidate_rect(rect_t r)
{
    for (window_t *w = bottom_window; w; w = w->above)
        if (!w->minimized && rects_overlap(w->frame, r))
            w->needs_redraw = true;
    any_dirty = true;
}

/* Record that `r` is no longer covered by any window and needs repainting
 * from the wallpaper up. Falls back to a full repaint if too many small
 * regions pile up before the next frame is drawn. */
static void wm_expose(rect_t r)
{
    if (rect_empty(r))
        return;
    if (exposed_count < WM_MAX_EXPOSED)
        exposed_rects[exposed_count++] = r;
    else
        full_redraw = true;
    any_dirty = true;
}

size_t wm_take_exposed(rect_t *out, size_t max)
{
    size_t n = exposed_count < max ? exposed_count : max;
    for (size_t i = 0; i < n; i++)
        out[i] = exposed_rects[i];
    exposed_count = 0;
    return n;
}

window_t *wm_focused(void)
{
    /* The focused window is the topmost visible one. */
    for (window_t *w = top_window; w; w = w->below)
        if (!w->minimized)
            return w;
    return NULL;
}

void wm_focus(window_t *win)
{
    if (!win || win == wm_focused())
        return;
    win->minimized = false;
    unlink_window(win);
    push_to_top(win);
    wm_invalidate_all();
}

void wm_set_title(window_t *win, const char *title)
{
    strlcpy(win->title, title, sizeof(win->title));
    wm_invalidate(win);
}

void wm_toggle_minimize(window_t *win)
{
    if (!win)
        return;
    if (win->minimized) {
        wm_focus(win); /* restoring changes z-order: a full repaint is simplest */
    } else {
        rect_t old     = win->frame;
        win->minimized = true;
        /* Drop it to the bottom so the next window gets focus. */
        unlink_window(win);
        win->above = bottom_window;
        win->below = NULL;
        if (bottom_window)
            bottom_window->below = win;
        bottom_window = win;
        if (!top_window)
            top_window = win;
        wm_expose(old);
        wm_invalidate_rect(old);
    }
}

window_t *wm_open(const app_class_t *cls, int width, int height,
                  const char *title, const char *arg)
{
    window_t *win = kcalloc(1, sizeof(window_t));
    if (!win) {
        klog("[wm  ] out of memory opening '%s'\n", title);
        return NULL;
    }

    rect_t space = wm_workspace();
    int    step   = (int)(window_count % 6) * 26;
    win->id       = next_id++;
    win->cls      = cls;
    win->frame    = rect_make(48 + step, 40 + step, width, height);

    if (win->frame.x + win->frame.w > space.w)
        win->frame.x = MAX(0, space.w - win->frame.w);
    if (win->frame.y + win->frame.h > space.h)
        win->frame.y = MAX(0, space.h - win->frame.h);
    strlcpy(win->title, title, sizeof(win->title));

    push_to_top(win);
    window_count++;

    if (cls->on_open)
        cls->on_open(win, arg);

    wm_invalidate_all();
    klog("[wm  ] opened window %d '%s' (%dx%d)\n", win->id, win->title, width,
         height);
    return win;
}

void wm_close(window_t *win)
{
    if (!win)
        return;
    if (dragging == win)
        dragging = NULL;
    if (resizing == win)
        resizing = NULL;
    if (win->cls->on_close)
        win->cls->on_close(win);

    rect_t old = win->frame;
    unlink_window(win);
    window_count--;
    klog("[wm  ] closed window %d '%s'\n", win->id, win->title);
    kfree(win);

    wm_expose(old);
    wm_invalidate_rect(old);
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

static void draw_button_glyph(rect_t r, bool close_button, color_t color)
{
    int cx = r.x + r.w / 2;
    int cy = r.y + r.h / 2;

    if (close_button) {
        for (int i = -4; i <= 4; i++) {
            gfx_pixel(cx + i, cy + i, color);
            gfx_pixel(cx + i, cy - i, color);
            gfx_pixel(cx + i + 1, cy + i, color);
            gfx_pixel(cx + i + 1, cy - i, color);
        }
    } else {
        gfx_fill_rect(cx - 4, cy + 3, 9, 2, color);
    }
}

void wm_draw_window(window_t *win)
{
    if (!win || win->minimized)
        return;

    bool    focused = (win == wm_focused());
    color_t title   = focused ? COLOR_TITLE_ACTIVE : COLOR_TITLE_INACTIVE;
    color_t border  = focused ? COLOR_ACCENT : COLOR_BORDER;

    gfx_clip_reset();
    gfx_clip_push(win->frame);

    /* Frame */
    gfx_fill_rect(win->frame.x, win->frame.y, win->frame.w, win->frame.h,
                  COLOR_WINDOW_BG);
    gfx_gradient_v(win->frame.x, win->frame.y, win->frame.w, TITLEBAR_HEIGHT,
                   title, focused ? COLOR_ACCENT_DARK : COLOR_PANEL);
    gfx_rect(win->frame.x, win->frame.y, win->frame.w, win->frame.h, border);
    gfx_hline(win->frame.x, win->frame.y + TITLEBAR_HEIGHT, win->frame.w,
              border);

    /* Title text, clipped so it can never overrun the buttons. */
    rect_t title_area =
        rect_make(win->frame.x + 8, win->frame.y,
                  win->frame.w - 2 * (BUTTON_SIZE + BUTTON_GAP) - 16,
                  TITLEBAR_HEIGHT);
    gfx_clip_push(title_area);
    gfx_text(title_area.x, win->frame.y + (TITLEBAR_HEIGHT - FONT_H) / 2,
             win->title, focused ? COLOR_TEXT : COLOR_TEXT_DIM);
    gfx_clip_pop();

    rect_t close = close_button_rect(win);
    rect_t mini  = minimize_button_rect(win);
    draw_button_glyph(close, true, focused ? COLOR_TEXT : COLOR_TEXT_DIM);
    draw_button_glyph(mini, false, focused ? COLOR_TEXT : COLOR_TEXT_DIM);

    /* Resize grip: three dots stepping diagonally into the corner. */
    rect_t  grip = resize_grip_rect(win);
    color_t dot  = focused ? COLOR_TEXT_DIM : COLOR_BORDER;
    for (int i = 0; i < 3; i++)
        gfx_fill_rect(grip.x + grip.w - 4 - i * 4,
                     grip.y + grip.h - 4 - i * 4, 2, 2, dot);

    /* Client area */
    rect_t client = wm_client_rect(win);
    gfx_clip_push(client);
    if (win->cls->on_draw)
        win->cls->on_draw(win, client);
    gfx_clip_pop();

    gfx_clip_pop();
    gfx_clip_reset();

    win->needs_redraw = false;
}

/* ------------------------------------------------------------------ */
/* Event routing                                                       */
/* ------------------------------------------------------------------ */

bool wm_handle_key(const key_event_t *ev)
{
    window_t *win = wm_focused();
    if (!win)
        return false;
    if (win->cls->on_key && win->cls->on_key(win, ev))
        return true;
    return false;
}

void wm_cycle_focus(void)
{
    if (window_count < 2)
        return;
    /* Move the current top window to the bottom, then focus the new top. */
    window_t *current = wm_focused();
    if (!current)
        return;
    unlink_window(current);
    current->above = bottom_window;
    current->below = NULL;
    if (bottom_window)
        bottom_window->below = current;
    bottom_window = current;
    if (!top_window)
        top_window = current;

    window_t *next = wm_focused();
    if (next)
        next->minimized = false;
    wm_invalidate_all();
}

static window_t *window_at(int x, int y)
{
    for (window_t *w = top_window; w; w = w->below)
        if (!w->minimized && rect_contains(w->frame, x, y))
            return w;
    return NULL;
}

bool wm_handle_mouse(const mouse_event_t *ev)
{
    /* An in-progress drag owns the mouse until the button is released. */
    if (dragging) {
        if (ev->released & MOUSE_LEFT) {
            dragging = NULL;
            return true;
        }
        rect_t space = wm_workspace();
        int    nx    = ev->x - drag_offset_x;
        int    ny    = ev->y - drag_offset_y;
        nx = CLAMP(nx, -dragging->frame.w + 80, space.w - 80);
        ny = CLAMP(ny, 0, space.h - TITLEBAR_HEIGHT);
        if (nx != dragging->frame.x || ny != dragging->frame.y) {
            rect_t old        = dragging->frame;
            dragging->frame.x = nx;
            dragging->frame.y = ny;
            wm_expose(old);
            wm_invalidate_rect(old);
            wm_invalidate(dragging);
        }
        return true;
    }

    /* Likewise for an in-progress resize. */
    if (resizing) {
        if (ev->released & MOUSE_LEFT) {
            resizing = NULL;
            return true;
        }
        rect_t space = wm_workspace();
        int nw = resize_start_w + (ev->x - resize_start_x);
        int nh = resize_start_h + (ev->y - resize_start_y);
        nw = CLAMP(nw, WINDOW_MIN_W, space.w - resizing->frame.x);
        nh = CLAMP(nh, WINDOW_MIN_H, space.h - resizing->frame.y);
        if (nw != resizing->frame.w || nh != resizing->frame.h) {
            rect_t old        = resizing->frame;
            resizing->frame.w = nw;
            resizing->frame.h = nh;
            wm_expose(old);
            wm_invalidate_rect(old);
            wm_invalidate(resizing);
            if (resizing->cls->on_resize)
                resizing->cls->on_resize(resizing, wm_client_rect(resizing));
        }
        return true;
    }

    window_t *win = window_at(ev->x, ev->y);
    if (!win)
        return false;

    if (ev->pressed & MOUSE_LEFT) {
        if (win != wm_focused())
            wm_focus(win);

        if (rect_contains(close_button_rect(win), ev->x, ev->y)) {
            wm_close(win);
            return true;
        }
        if (rect_contains(minimize_button_rect(win), ev->x, ev->y)) {
            wm_toggle_minimize(win);
            return true;
        }
        if (rect_contains(resize_grip_rect(win), ev->x, ev->y)) {
            resizing       = win;
            resize_start_w = win->frame.w;
            resize_start_h = win->frame.h;
            resize_start_x = ev->x;
            resize_start_y = ev->y;
            return true;
        }
        if (rect_contains(titlebar_rect(win), ev->x, ev->y)) {
            dragging      = win;
            drag_offset_x = ev->x - win->frame.x;
            drag_offset_y = ev->y - win->frame.y;
            return true;
        }
    }

    rect_t client = wm_client_rect(win);
    if (rect_contains(client, ev->x, ev->y) && win->cls->on_mouse)
        return win->cls->on_mouse(win, ev, ev->x - client.x, ev->y - client.y);

    return true; /* the click landed on the frame: consume it */
}
