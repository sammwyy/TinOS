/* Desktop environment: wallpaper, taskbar, start menu, mouse cursor and the
 * event loop that ties the drivers, the window manager and the applications
 * together.
 *
 * Compositing works on damage: windows repaint themselves when their content
 * changes, and the whole screen is only repainted when the geometry changes
 * (a window opened, closed, moved or was minimised).  The cursor is painted
 * on top of the finished frame and lifted again afterwards, so moving the
 * mouse only uploads a few hundred pixels.
 */

#include <tinos/arch.h>
#include <tinos/desktop.h>
#include <tinos/fs.h>
#include <tinos/gfx.h>
#include <tinos/heap.h>
#include <tinos/input.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/printf.h>
#include <tinos/sh.h>
#include <tinos/string.h>
#include <tinos/ui.h>

#define MENU_WIDTH      252
#define MENU_ITEM_H     30
#define MENU_HEADER_H   44
#define START_BUTTON_W  92
#define TASK_BUTTON_W   162
#define CLOCK_WIDTH     104
#define WIDGET_AREA_W   (CLOCK_WIDTH + 124) /* clock + memory gauge + label */
#define NOTIFY_MS       2600

typedef enum {
    MENU_ACTION_APP = 0,
    MENU_ACTION_SEPARATOR,
    MENU_ACTION_RESTART,
    MENU_ACTION_SHUTDOWN,
} menu_action_t;

typedef struct {
    const char   *label;
    const char   *hint;
    icon_t        icon;
    menu_action_t action;
    const app_entry_t *app;
} menu_item_t;

static menu_item_t menu_items[16];
static size_t      menu_item_count;

static bool   menu_open;
static int    menu_index;
static bool   taskbar_dirty = true;
static bool   menu_dirty    = true;
static char   notification[96];
static uint64_t notification_until;
static uint32_t last_clock_second = 0xFFFFFFFF;

/* ------------------------------------------------------------------ */
/* Mouse cursor                                                        */
/* ------------------------------------------------------------------ */

#define CURSOR_W 12
#define CURSOR_H 19

/* '#' outline, '.' fill, ' ' transparent */
static const char *const cursor_shape[CURSOR_H] = {
    "#           ",
    "##          ",
    "#.#         ",
    "#..#        ",
    "#...#       ",
    "#....#      ",
    "#.....#     ",
    "#......#    ",
    "#.......#   ",
    "#........#  ",
    "#.....##### ",
    "#..#..#     ",
    "#.# #..#    ",
    "##  #..#    ",
    "#    #..#   ",
    "     #..#   ",
    "      #..#  ",
    "      #..#  ",
    "       ##   ",
};

static uint32_t cursor_backup[CURSOR_W * CURSOR_H];
static rect_t   cursor_rect;
static bool     cursor_drawn;

static void cursor_save_and_draw(int cx, int cy)
{
    const fb_info_t *fb = gfx_info();
    rect_t area = rect_intersect(rect_make(cx, cy, CURSOR_W, CURSOR_H),
                                 rect_make(0, 0, fb->width, fb->height));
    cursor_rect = area;
    if (rect_empty(area)) {
        cursor_drawn = false;
        return;
    }

    for (int y = 0; y < area.h; y++)
        for (int x = 0; x < area.w; x++)
            cursor_backup[y * CURSOR_W + x] =
                fb->back[(size_t)(area.y + y) * fb->width + area.x + x];

    for (int y = 0; y < area.h; y++) {
        const char *row = cursor_shape[area.y + y - cy];
        for (int x = 0; x < area.w; x++) {
            char c = row[area.x + x - cx];
            if (c == ' ')
                continue;
            fb->back[(size_t)(area.y + y) * fb->width + area.x + x] =
                (c == '#') ? RGB(0x10, 0x12, 0x18) : RGB(0xff, 0xff, 0xff);
        }
    }
    gfx_mark_dirty(area.x, area.y, area.w, area.h);
    cursor_drawn = true;
}

static void cursor_restore(void)
{
    if (!cursor_drawn)
        return;
    const fb_info_t *fb = gfx_info();
    for (int y = 0; y < cursor_rect.h; y++)
        for (int x = 0; x < cursor_rect.w; x++)
            fb->back[(size_t)(cursor_rect.y + y) * fb->width + cursor_rect.x +
                     x] = cursor_backup[y * CURSOR_W + x];
    cursor_drawn = false;
}

/* ------------------------------------------------------------------ */
/* Wallpaper                                                           */
/* ------------------------------------------------------------------ */

static void draw_wallpaper(void)
{
    rect_t space = wm_workspace();

    gfx_clip_reset();
    gfx_gradient_v(0, 0, space.w, space.h, COLOR_DESKTOP_TOP,
                   COLOR_DESKTOP_BOTTOM);

    /* A few faint stars so the background is not a flat wash. */
    uint32_t seed = 0x1234567u;
    for (int i = 0; i < 220; i++) {
        seed      = seed * 1103515245u + 12345u;
        int x     = (int)((seed >> 8) % (uint32_t)space.w);
        seed      = seed * 1103515245u + 12345u;
        int y     = (int)((seed >> 8) % (uint32_t)space.h);
        int shade = 0x30 + (int)((seed >> 20) & 0x5F);
        gfx_pixel(x, y, RGB(shade, shade, shade + 0x20));
    }

    /* Wordmark in the middle of the desktop. */
    int cx = space.w / 2;
    int cy = space.h / 2 - 40;
    widget_icon(cx - 24, cy - 60, 48, ICON_TINOS);

    const char *title = TINOS_NAME " " TINOS_VERSION;
    gfx_text(cx - gfx_text_width(title) / 2, cy, title, COLOR_TEXT);

    const char *lines[] = {
        "Press Ctrl+Esc (or click Start) to open the start menu",
        "Alt+Tab switches windows, Alt+F4 closes the focused one",
    };
    for (size_t i = 0; i < ARRAY_SIZE(lines); i++)
        gfx_text(cx - gfx_text_width(lines[i]) / 2, cy + 28 + (int)i * 20,
                 lines[i], COLOR_TEXT_DIM);
}

/* ------------------------------------------------------------------ */
/* Taskbar                                                             */
/* ------------------------------------------------------------------ */

static rect_t taskbar_rect(void)
{
    return rect_make(0, gfx_height() - TASKBAR_HEIGHT, gfx_width(),
                     TASKBAR_HEIGHT);
}

static rect_t start_button_rect(void)
{
    rect_t bar = taskbar_rect();
    return rect_make(bar.x + 4, bar.y + 4, START_BUTTON_W,
                     TASKBAR_HEIGHT - 8);
}

static rect_t task_button_rect(size_t index)
{
    rect_t bar   = taskbar_rect();
    int    start = START_BUTTON_W + 12;
    int    width = TASK_BUTTON_W;
    int    avail = bar.w - start - WIDGET_AREA_W - 8;
    size_t count = wm_count();
    if (count && (int)(count * (size_t)(width + 4)) > avail)
        width = MAX(70, avail / (int)count - 4);
    return rect_make(start + (int)index * (width + 4), bar.y + 4, width,
                     TASKBAR_HEIGHT - 8);
}

static void format_clock(char *buf, size_t size)
{
    rtc_time_t now;
    rtc_read(&now);
    if (now.valid)
        ksnprintf(buf, size, "%02u:%02u:%02u", now.hour, now.minute,
                  now.second);
    else {
        uint32_t s = (uint32_t)(timer_ticks() / 1000);
        ksnprintf(buf, size, "up %02u:%02u:%02u", s / 3600, (s / 60) % 60,
                  s % 60);
    }
}

static icon_t window_icon(const window_t *win)
{
    if (!win->cls || !win->cls->name)
        return ICON_FILE;
    if (strcmp(win->cls->name, "terminal") == 0)
        return ICON_TERMINAL;
    if (strcmp(win->cls->name, "explorer") == 0)
        return ICON_FOLDER;
    if (strcmp(win->cls->name, "editor") == 0)
        return ICON_EDITOR;
    if (strcmp(win->cls->name, "sysinfo") == 0)
        return ICON_CHIP;
    return ICON_INFO;
}

static void draw_taskbar(void)
{
    rect_t bar = taskbar_rect();

    gfx_clip_reset();
    gfx_clip_push(bar);
    gfx_fill_rect(bar.x, bar.y, bar.w, bar.h, COLOR_TASKBAR);
    gfx_hline(bar.x, bar.y, bar.w, COLOR_TASKBAR_EDGE);

    /* Start button */
    rect_t start = start_button_rect();
    gfx_fill_rect(start.x, start.y, start.w, start.h,
                  menu_open ? COLOR_ACCENT_DARK : COLOR_ACCENT);
    gfx_rect(start.x, start.y, start.w, start.h, COLOR_ACCENT_DARK);
    widget_icon(start.x + 5, start.y + 4, 18, ICON_TINOS);
    gfx_text(start.x + 30, start.y + (start.h - FONT_H) / 2, "Start",
             COLOR_TEXT);

    /* One button per window, highlighting the focused one. */
    window_t *focused = wm_focused();
    size_t    index   = 0;
    for (window_t *w = wm_bottom(); w; w = w->above, index++) {
        rect_t r = task_button_rect(index);
        if (r.x + r.w > bar.w - WIDGET_AREA_W)
            break;
        bool active = (w == focused) && !w->minimized;
        gfx_fill_rect(r.x, r.y, r.w, r.h,
                      active ? COLOR_PANEL_LIGHT : COLOR_PANEL);
        gfx_rect(r.x, r.y, r.w, r.h, active ? COLOR_ACCENT : COLOR_BORDER);
        if (active)
            gfx_hline(r.x + 1, r.y + r.h - 2, r.w - 2, COLOR_ACCENT);
        widget_icon(r.x + 4, r.y + 4, 16, window_icon(w));

        gfx_clip_push(rect_make(r.x + 24, r.y, r.w - 28, r.h));
        gfx_text(r.x + 26, r.y + (r.h - FONT_H) / 2, w->title,
                 w->minimized ? COLOR_TEXT_DIM : COLOR_TEXT);
        gfx_clip_pop();
    }

    /* Right hand widget area: separator, memory gauge and the clock. */
    int right = bar.w - CLOCK_WIDTH - 8;
    gfx_vline(right - 112, bar.y + 7, bar.h - 14, COLOR_BORDER);

    heap_stats_t heap;
    heap_get_stats(&heap);
    int gauge_x = right - 68;
    int gauge_w = 58;
    int gauge_y = bar.y + (TASKBAR_HEIGHT - 10) / 2;
    int used    = heap.heap_total
                      ? (int)((uint64_t)heap.heap_used * gauge_w /
                              heap.heap_total)
                      : 0;
    gfx_text(right - 100, bar.y + (TASKBAR_HEIGHT - FONT_H) / 2, "mem",
             COLOR_TEXT_DIM);
    gfx_fill_rect(gauge_x, gauge_y, gauge_w, 10, RGB(0x14, 0x18, 0x22));
    gfx_fill_rect(gauge_x, gauge_y, CLAMP(used, 1, gauge_w), 10, COLOR_ACCENT);
    gfx_rect(gauge_x, gauge_y, gauge_w, 10, COLOR_BORDER);

    char clock[32];
    format_clock(clock, sizeof(clock));
    gfx_fill_rect(right, bar.y + 4, CLOCK_WIDTH, bar.h - 8, COLOR_PANEL);
    gfx_rect(right, bar.y + 4, CLOCK_WIDTH, bar.h - 8, COLOR_BORDER);
    widget_text_centered(rect_make(right, bar.y + 4, CLOCK_WIDTH, bar.h - 8),
                         clock, COLOR_TEXT);

    gfx_clip_pop();
    taskbar_dirty = false;
}

/* ------------------------------------------------------------------ */
/* Start menu                                                          */
/* ------------------------------------------------------------------ */

static void build_menu(void)
{
    size_t             app_count = 0;
    const app_entry_t *apps      = apps_registry(&app_count);

    menu_item_count = 0;
    for (size_t i = 0; i < app_count && menu_item_count < ARRAY_SIZE(menu_items);
         i++) {
        menu_items[menu_item_count++] = (menu_item_t){
            apps[i].name, apps[i].summary, apps[i].icon, MENU_ACTION_APP,
            &apps[i]
        };
    }
    menu_items[menu_item_count++] =
        (menu_item_t){ NULL, NULL, ICON_INFO, MENU_ACTION_SEPARATOR, NULL };
    menu_items[menu_item_count++] = (menu_item_t){
        "Restart", "Reset this machine", ICON_RESTART,
        MENU_ACTION_RESTART, NULL
    };
    menu_items[menu_item_count++] = (menu_item_t){
        "Shut Down", "Power off this machine", ICON_POWER,
        MENU_ACTION_SHUTDOWN, NULL
    };
}

static int menu_height(void)
{
    int height = MENU_HEADER_H + 8;
    for (size_t i = 0; i < menu_item_count; i++)
        height += menu_items[i].action == MENU_ACTION_SEPARATOR ? 9
                                                                : MENU_ITEM_H;
    return height + 6;
}

static rect_t menu_rect(void)
{
    int h = menu_height();
    return rect_make(4, gfx_height() - TASKBAR_HEIGHT - h, MENU_WIDTH, h);
}

static rect_t menu_item_rect(size_t index)
{
    rect_t r = menu_rect();
    int    y = r.y + MENU_HEADER_H + 6;
    for (size_t i = 0; i < index && i < menu_item_count; i++)
        y += menu_items[i].action == MENU_ACTION_SEPARATOR ? 9 : MENU_ITEM_H;
    int h = menu_items[index].action == MENU_ACTION_SEPARATOR ? 9 : MENU_ITEM_H;
    return rect_make(r.x + 4, y, r.w - 8, h);
}

static void draw_menu(void)
{
    if (!menu_open)
        return;

    rect_t r = menu_rect();
    gfx_clip_reset();
    gfx_clip_push(r);

    widget_panel(r, COLOR_PANEL, COLOR_ACCENT);
    gfx_gradient_v(r.x + 1, r.y + 1, r.w - 2, MENU_HEADER_H, COLOR_ACCENT_DARK,
                   COLOR_PANEL);
    widget_icon(r.x + 10, r.y + 8, 26, ICON_TINOS);
    gfx_text(r.x + 46, r.y + 10, TINOS_NAME " " TINOS_VERSION, COLOR_TEXT);
    gfx_text(r.x + 46, r.y + 26, "applications", COLOR_TEXT_DIM);

    for (size_t i = 0; i < menu_item_count; i++) {
        rect_t item = menu_item_rect(i);
        if (menu_items[i].action == MENU_ACTION_SEPARATOR) {
            gfx_hline(item.x + 6, item.y + 4, item.w - 12, COLOR_BORDER);
            continue;
        }
        bool selected = (int)i == menu_index;
        if (selected) {
            gfx_fill_rect(item.x, item.y, item.w, item.h, COLOR_SELECTION);
            gfx_rect(item.x, item.y, item.w, item.h, COLOR_ACCENT);
        }
        widget_icon(item.x + 6, item.y + (item.h - 18) / 2, 18,
                    menu_items[i].icon);
        gfx_text(item.x + 32, item.y + 4, menu_items[i].label, COLOR_TEXT);
        gfx_clip_push(rect_make(item.x + 32, item.y, item.w - 36, item.h));
        gfx_text(item.x + 32, item.y + 16, menu_items[i].hint,
                 selected ? COLOR_TEXT : COLOR_TEXT_DIM);
        gfx_clip_pop();
    }

    gfx_clip_pop();
    menu_dirty = false;
}

static void menu_toggle(void)
{
    menu_open = !menu_open;
    if (menu_open) {
        menu_index = 0;
        menu_dirty = true;
    }
    /* Opening or closing the menu uncovers part of the desktop. */
    wm_invalidate_all();
    taskbar_dirty = true;
}

static void menu_move(int delta)
{
    if (menu_item_count == 0)
        return;
    int index = menu_index;
    for (int guard = 0; guard < (int)menu_item_count * 2; guard++) {
        index += delta;
        if (index < 0)
            index = (int)menu_item_count - 1;
        if (index >= (int)menu_item_count)
            index = 0;
        if (menu_items[index].action != MENU_ACTION_SEPARATOR)
            break;
    }
    menu_index = index;
    menu_dirty = true;
}

static void menu_activate(size_t index)
{
    if (index >= menu_item_count)
        return;
    menu_item_t *item = &menu_items[index];

    switch (item->action) {
    case MENU_ACTION_APP:
        menu_open = false;
        wm_invalidate_all();
        taskbar_dirty = true;
        if (item->app && item->app->launch)
            item->app->launch(NULL);
        break;
    case MENU_ACTION_RESTART:
        desktop_shutdown(true);
        break;
    case MENU_ACTION_SHUTDOWN:
        desktop_shutdown(false);
        break;
    case MENU_ACTION_SEPARATOR:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Notifications                                                       */
/* ------------------------------------------------------------------ */

void desktop_notify(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(notification, sizeof(notification), fmt, ap);
    va_end(ap);

    notification_until = timer_ticks() + NOTIFY_MS;
    wm_invalidate_all();
    klog("[note] %s\n", notification);
}

static void draw_notification(void)
{
    if (!notification[0])
        return;
    int    width = gfx_text_width(notification) + 32;
    rect_t r     = rect_make((gfx_width() - width) / 2,
                             gfx_height() - TASKBAR_HEIGHT - 52, width, 32);
    gfx_clip_reset();
    widget_panel(r, COLOR_PANEL_LIGHT, COLOR_ACCENT);
    widget_text_centered(r, notification, COLOR_TEXT);
}

/* ------------------------------------------------------------------ */
/* Shutdown                                                            */
/* ------------------------------------------------------------------ */

NORETURN void desktop_shutdown(bool reboot)
{
    const char *message = reboot ? "Restarting TinOS ..."
                                 : "TinOS has been shut down.";
    gfx_clip_reset();
    gfx_clear(RGB(0x0b, 0x10, 0x1c));
    widget_icon(gfx_width() / 2 - 24, gfx_height() / 2 - 90, 48, ICON_TINOS);
    gfx_text(gfx_width() / 2 - gfx_text_width(message) / 2, gfx_height() / 2,
             message, COLOR_TEXT);
    if (!reboot) {
        const char *hint = "It is now safe to close the QEMU window.";
        gfx_text(gfx_width() / 2 - gfx_text_width(hint) / 2,
                 gfx_height() / 2 + 24, hint, COLOR_TEXT_DIM);
    }
    gfx_mark_all_dirty();
    gfx_present();

    klog("[boot] %s\n", message);
    timer_sleep(900);

    if (reboot)
        arch_reboot();
    arch_shutdown();
}

/* ------------------------------------------------------------------ */
/* Input handling                                                      */
/* ------------------------------------------------------------------ */

static bool handle_global_key(const key_event_t *ev)
{
    /* Start menu navigation swallows keys while it is open. */
    if (menu_open) {
        switch (ev->code) {
        case KEY_ESCAPE:
            menu_toggle();
            return true;
        case KEY_UP:
            menu_move(-1);
            return true;
        case KEY_DOWN:
            menu_move(1);
            return true;
        case KEY_ENTER:
            menu_activate((size_t)menu_index);
            return true;
        default:
            break;
        }
    }

    if ((ev->mods & KMOD_CTRL) && ev->code == KEY_ESCAPE) {
        menu_toggle();
        return true;
    }
    if (ev->code == KEY_F12) {
        menu_toggle();
        return true;
    }
    if ((ev->mods & KMOD_ALT) && ev->code == KEY_TAB) {
        wm_cycle_focus();
        return true;
    }
    if ((ev->mods & KMOD_ALT) && ev->code == KEY_F4) {
        window_t *focused = wm_focused();
        if (focused)
            wm_close(focused);
        taskbar_dirty = true;
        return true;
    }
    if ((ev->mods & KMOD_CTRL) && (ev->mods & KMOD_ALT)) {
        if (ev->ch == 't' || ev->ch == 'T') {
            app_terminal_open(NULL);
            return true;
        }
        if (ev->ch == 'e' || ev->ch == 'E') {
            app_explorer_open(NULL);
            return true;
        }
    }
    return false;
}

static bool handle_taskbar_click(const mouse_event_t *ev)
{
    if (!(ev->pressed & MOUSE_LEFT))
        return true;

    if (rect_contains(start_button_rect(), ev->x, ev->y)) {
        menu_toggle();
        return true;
    }

    size_t index = 0;
    for (window_t *w = wm_bottom(); w; w = w->above, index++) {
        if (!rect_contains(task_button_rect(index), ev->x, ev->y))
            continue;
        if (w == wm_focused() && !w->minimized)
            wm_toggle_minimize(w);
        else
            wm_focus(w);
        taskbar_dirty = true;
        return true;
    }
    return true;
}

static void handle_mouse_event(const mouse_event_t *ev)
{
    if (menu_open) {
        rect_t r = menu_rect();
        if (rect_contains(r, ev->x, ev->y)) {
            for (size_t i = 0; i < menu_item_count; i++) {
                if (menu_items[i].action == MENU_ACTION_SEPARATOR)
                    continue;
                if (rect_contains(menu_item_rect(i), ev->x, ev->y)) {
                    if ((int)i != menu_index) {
                        menu_index = (int)i;
                        menu_dirty = true;
                    }
                    if (ev->pressed & MOUSE_LEFT)
                        menu_activate(i);
                    return;
                }
            }
            return;
        }
        if (ev->pressed & MOUSE_LEFT) {
            /* A click outside an open menu just dismisses it. */
            menu_toggle();
            return;
        }
    }

    if (rect_contains(taskbar_rect(), ev->x, ev->y)) {
        handle_taskbar_click(ev);
        return;
    }

    if (!wm_handle_mouse(ev)) {
        /* Clicking the wallpaper drops focus decorations but nothing else. */
    }
}

/* ------------------------------------------------------------------ */
/* Compositing                                                         */
/* ------------------------------------------------------------------ */

static bool rects_overlap(rect_t a, rect_t b)
{
    return !rect_empty(rect_intersect(a, b));
}

static void composite(bool full)
{
    if (full) {
        draw_wallpaper();
        for (window_t *w = wm_bottom(); w; w = w->above)
            wm_draw_window(w);
        draw_taskbar();
        draw_menu();
        draw_notification();
        return;
    }

    for (window_t *w = wm_bottom(); w; w = w->above) {
        if (!w->needs_redraw || w->minimized)
            continue;
        rect_t area = w->frame;
        wm_draw_window(w);

        /* Anything stacked above the repainted window must be redrawn too. */
        for (window_t *upper = w->above; upper; upper = upper->above)
            if (!upper->minimized && rects_overlap(upper->frame, area))
                wm_draw_window(upper);
        if (rects_overlap(taskbar_rect(), area))
            taskbar_dirty = true;
        if (menu_open && rects_overlap(menu_rect(), area))
            menu_dirty = true;
    }

    if (taskbar_dirty)
        draw_taskbar();
    if (menu_open && menu_dirty)
        draw_menu();
    if (notification[0])
        draw_notification();
}

/* ------------------------------------------------------------------ */
/* Event loop                                                          */
/* ------------------------------------------------------------------ */

void desktop_run(void)
{
    wm_init();
    build_menu();
    mouse_set_bounds(gfx_width(), gfx_height());

    klog("[desk] desktop starting (%dx%d, %u menu entries)\n", gfx_width(),
         gfx_height(), (uint32_t)menu_item_count);

    composite(true);
    int mx, my;
    mouse_get_position(&mx, &my);
    cursor_save_and_draw(mx, my);
    gfx_present();
    cursor_restore();

    uint64_t last_frame   = 0;
    bool     pending_full = false;

    for (;;) {
        bool dirty = false;

        /* --- keyboard --- */
        key_event_t key;
        while (keyboard_poll(&key)) {
            if (handle_global_key(&key))
                dirty = true;
            else if (wm_handle_key(&key))
                dirty = true;
        }

        /* --- mouse --- */
        mouse_event_t mouse;
        bool          moved = false;
        while (mouse_poll(&mouse)) {
            handle_mouse_event(&mouse);
            moved = true;
        }

        /* --- application ticks --- */
        for (window_t *w = wm_bottom(); w; w = w->above)
            if (w->cls->on_tick && w->cls->on_tick(w))
                dirty = true;

        /* --- clock --- */
        uint32_t second = (uint32_t)(timer_ticks() / 1000);
        if (second != last_clock_second) {
            last_clock_second = second;
            taskbar_dirty     = true;
            dirty             = true;
        }

        /* --- notification expiry --- */
        if (notification[0] && timer_ticks() > notification_until) {
            notification[0] = '\0';
            wm_invalidate_all();
            dirty = true;
        }

        /* A full repaint may be requested in a frame we end up skipping, so
         * the request has to survive until we actually draw. */
        pending_full |= wm_take_full_redraw();
        if (wm_any_dirty() || taskbar_dirty || menu_dirty)
            dirty = true;

        uint64_t now = timer_ticks();
        if ((dirty || moved) && now - last_frame >= 15) {
            last_frame = now;

            /* Repair the area the cursor covered in the previous frame. */
            if (!rect_empty(cursor_rect))
                gfx_mark_dirty(cursor_rect.x, cursor_rect.y, cursor_rect.w,
                               cursor_rect.h);
            if (dirty || pending_full) {
                composite(pending_full);
                pending_full = false;
            }

            mouse_get_position(&mx, &my);
            cursor_save_and_draw(mx, my);
            gfx_present();
            cursor_restore();
        }

        __asm__ volatile("sti; hlt");
    }
}
