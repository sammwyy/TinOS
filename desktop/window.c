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

#define BUTTON_SIZE TITLEBAR_HEIGHT
#define BUTTON_GAP  0

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

/* Geometry */

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

static rect_t maximize_button_rect(const window_t *win)
{
    rect_t close = close_button_rect(win);
    return rect_make(close.x - BUTTON_SIZE - BUTTON_GAP, close.y,
                     BUTTON_SIZE, BUTTON_SIZE);
}

static rect_t minimize_button_rect(const window_t *win)
{
    rect_t maximize = maximize_button_rect(win);
    return rect_make(maximize.x - BUTTON_SIZE - BUTTON_GAP, maximize.y,
                     BUTTON_SIZE, BUTTON_SIZE);
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

/* List handling */

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

static bool surface_resize(window_t *win)
{
    size_t words = (size_t)MAX(0, win->frame.w) *
                   (size_t)MAX(0, win->frame.h);
    if (win->surface && win->surface_words == words)
        return true;
    if (win->surface)
        kfree(win->surface);
    win->surface = NULL;
    win->surface_words = 0;
    win->surface_valid = false;
    if (!words)
        return false;
    win->surface = (uint32_t *)kmalloc(words * sizeof(uint32_t));
    if (!win->surface)
        return false;
    win->surface_words = words;
    return true;
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
    for (size_t i = 0; i < exposed_count; i++) {
        rect_t old = exposed_rects[i];
        if (r.x > old.x + old.w || old.x > r.x + r.w ||
            r.y > old.y + old.h || old.y > r.y + r.h)
            continue;
        int x0 = MIN(old.x, r.x);
        int y0 = MIN(old.y, r.y);
        int x1 = MAX(old.x + old.w, r.x + r.w);
        int y1 = MAX(old.y + old.h, r.y + r.h);
        exposed_rects[i] = rect_make(x0, y0, x1 - x0, y1 - y0);
        return;
    }
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
    win->frame    = rect_make(48 + step, 40 + step,
                              MIN(width, space.w), MIN(height, space.h));

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
    klog("[wm  ] opened window %d '%s' (%dx%d)\n", win->id, win->title,
         win->frame.w, win->frame.h);
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
    if (win->surface)
        kfree(win->surface);
    klog("[wm  ] closed window %d '%s'\n", win->id, win->title);
    kfree(win);

    wm_expose(old);
    wm_invalidate_rect(old);
}

/* Drawing */

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

static void draw_maximize_glyph(rect_t r, color_t color)
{
    gfx_rect(r.x + 4, r.y + 4, r.w - 8, r.h - 8, color);
    gfx_hline(r.x + 4, r.y + 4, r.w - 8, color);
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
    gfx_bevel(win->frame.x, win->frame.y, win->frame.w, win->frame.h,
              COLOR_PANEL_LIGHT, COLOR_BORDER);
    gfx_gradient_v(win->frame.x, win->frame.y, win->frame.w, TITLEBAR_HEIGHT,
                   title, focused ? COLOR_ACCENT_DARK : COLOR_PANEL);
    gfx_rect(win->frame.x, win->frame.y, win->frame.w, win->frame.h, border);
    gfx_hline(win->frame.x, win->frame.y + TITLEBAR_HEIGHT, win->frame.w,
              border);

    /* Center the caption in the frame, independently of the controls. */
    rect_t title_area = rect_make(win->frame.x + 8, win->frame.y,
                                  win->frame.w - 16, TITLEBAR_HEIGHT);
    gfx_clip_push(title_area);
    int title_x = win->frame.x + (win->frame.w - gfx_text_width(win->title)) / 2;
    gfx_text(title_x, win->frame.y + (TITLEBAR_HEIGHT - FONT_H) / 2,
             win->title, RGB(0xf5, 0xf5, 0xfa));
    gfx_clip_pop();

    rect_t close = close_button_rect(win);
    rect_t mini  = minimize_button_rect(win);
    rect_t maxi  = maximize_button_rect(win);
    gfx_fill_rect(maxi.x, maxi.y, maxi.w, maxi.h, title);
    gfx_bevel(maxi.x + 1, maxi.y + 1, maxi.w - 2, maxi.h - 2,
              RGB(0xa4, 0xad, 0xc7), COLOR_ACCENT_DARK);
    gfx_vline(maxi.x, maxi.y, maxi.h, RGB(0xd0, 0xd5, 0xe2));
    gfx_fill_rect(mini.x, mini.y, mini.w, mini.h, title);
    gfx_bevel(mini.x + 1, mini.y + 1, mini.w - 2, mini.h - 2,
              RGB(0xa4, 0xad, 0xc7), COLOR_ACCENT_DARK);
    gfx_vline(mini.x, mini.y, mini.h, RGB(0xd0, 0xd5, 0xe2));
    gfx_fill_rect(close.x, close.y, close.w, close.h, title);
    gfx_bevel(close.x + 1, close.y + 1, close.w - 2, close.h - 2,
              RGB(0xa4, 0xad, 0xc7), COLOR_ACCENT_DARK);
    gfx_vline(close.x, close.y, close.h, RGB(0xd0, 0xd5, 0xe2));
    draw_maximize_glyph(maxi, RGB(0xff, 0xff, 0xff));
    draw_button_glyph(close, true, RGB(0xff, 0xff, 0xff));
    draw_button_glyph(mini, false, RGB(0xff, 0xff, 0xff));

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

    if (surface_resize(win)) {
        const fb_info_t *fb = gfx_info();
        rect_t visible = rect_intersect(
            win->frame, rect_make(0, 0, fb->width, fb->height));
        for (int y = visible.y; y < visible.y + visible.h; y++) {
            int sy = y - win->frame.y;
            int sx = visible.x - win->frame.x;
            memcpy(win->surface + (size_t)sy * win->frame.w + sx,
                   fb->back + (size_t)y * fb->width + visible.x,
                   (size_t)visible.w * sizeof(uint32_t));
        }
        win->surface_valid = true;
    }
    win->needs_redraw = false;
}

bool wm_window_cached(const window_t *win)
{
    return win && win->surface_valid && win->surface &&
           win->surface_words ==
               (size_t)MAX(0, win->frame.w) * (size_t)MAX(0, win->frame.h);
}

void wm_blit_window(window_t *win)
{
    if (!wm_window_cached(win))
        return;
    const fb_info_t *fb = gfx_info();
    rect_t visible = rect_intersect(win->frame,
                                    rect_make(0, 0, fb->width, fb->height));
    visible = rect_intersect(visible, gfx_clip_current());
    if (rect_empty(visible))
        return;
    for (int y = visible.y; y < visible.y + visible.h; y++) {
        int sy = y - win->frame.y;
        int sx = visible.x - win->frame.x;
        memcpy(fb->back + (size_t)y * fb->width + visible.x,
               win->surface + (size_t)sy * win->frame.w + sx,
               (size_t)visible.w * sizeof(uint32_t));
    }
    gfx_mark_dirty(visible.x, visible.y, visible.w, visible.h);
}

/* Event routing */

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
            wm_expose(dragging->frame);
            wm_invalidate_rect(old);
            /* The window contents did not change; use its cached surface. */
            dragging->needs_redraw = false;
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
        if (rect_contains(maximize_button_rect(win), ev->x, ev->y)) {
            if (win->maximized) {
                win->frame = win->restore_frame;
                win->maximized = false;
            } else {
                win->restore_frame = win->frame;
                win->frame = wm_workspace();
                win->maximized = true;
            }
            wm_invalidate_all();
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
