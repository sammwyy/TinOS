/* Desktop environment: wallpaper and desktop icons, taskbar with status
 * tray, start menu, context menus, tooltips, the mouse cursor and the event
 * loop that ties the drivers, the window manager and the applications
 * together.
 *
 * Compositing works on damage: windows repaint themselves when their content
 * changes, and the whole screen is only repainted when the geometry changes
 * (a window opened, closed, moved or was minimised).  The cursor is painted
 * on top of the finished frame and lifted again afterwards, so moving the
 * mouse only uploads a few hundred pixels.  Overlays (menus, popups,
 * tooltips) are drawn last and hand their area back to the window manager
 * as an exposed rectangle when they close.
 */

#include <tinos/arch.h>
#include <tinos/audio.h>
#include <tinos/bus.h>
#include <tinos/desktop.h>
#include <tinos/fs.h>
#include <tinos/gfx.h>
#include <tinos/heap.h>
#include <tinos/input.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/mm.h>
#include <tinos/net.h>
#include <tinos/printf.h>
#include <tinos/process.h>
#include <tinos/sh.h>
#include <tinos/string.h>
#include <tinos/ui.h>
#include <tinos/user_window.h>

#define MENU_WIDTH      160
#define MENU_SUBMENU_W  220
#define MENU_ITEM_H     24
#define MENU_CAT_H      24
#define MENU_HEADER_H   38
#define MENU_MAX_CATS   8
#define MENU_MAX_ROWS   24
#define START_BUTTON_W  84
#define TASK_BUTTON_W   148
#define CLOCK_WIDTH     58
#define TRAY_ICON_W     22
#define TRAY_MEMORY_W   44
#define TRAY_GAP        2
#define TRAY_WIDTH      (TRAY_ICON_W * 2 + TRAY_MEMORY_W + CLOCK_WIDTH + \
                         TRAY_GAP * 3 + 10)
#define NOTIFY_MS       2600
#define DOUBLE_CLICK_MS 450
#define TOOLTIP_DELAY   600

#define DESKTOP_MAX_ICONS 8
#define DESKTOP_CELL_W    84
#define DESKTOP_CELL_H    80

#define POPUP_MAX_ITEMS 12
#define POPUP_ITEM_H    22

#define PATH_TERMINAL "/apps/gui/terminal.elf"
#define PATH_FILES    "/apps/gui/explorer.elf"
#define PATH_TASKMGR  "/apps/gui/taskmgr.elf"
#define PATH_SETTINGS "/apps/gui/control_panel.elf"
#define PATH_ABOUT    "/apps/gui/about.elf"
#define PATH_SYSINFO  "/apps/gui/sysinfo.elf"

typedef enum {
    ROW_APP = 0,
    ROW_CATEGORY,
    ROW_SEPARATOR,
    ROW_TASKMGR,
    ROW_RESTART,
    ROW_SHUTDOWN,
} menu_row_kind_t;

typedef struct {
    const char *name;
    icon_t      icon;
} menu_category_t;

typedef struct {
    menu_row_kind_t   kind;
    size_t            cat; /* ROW_CATEGORY: index into menu_cats */
    const app_entry_t *app; /* ROW_APP: the application          */
} menu_row_t;

typedef enum {
    TRAY_NETWORK,
    TRAY_VOLUME,
    TRAY_MEMORY,
    TRAY_CLOCK,
    TRAY_COUNT,
} tray_item_t;

typedef enum {
    ACT_NONE,
    ACT_LAUNCH,        /* arg: executable path */
    ACT_SHOW_DESKTOP,
    ACT_WIN_RESTORE,
    ACT_WIN_MINIMIZE,
    ACT_WIN_MAXIMIZE,
    ACT_WIN_CLOSE,
    ACT_TOGGLE_MUTE,
    ACT_RESTART,
    ACT_SHUTDOWN,
} action_t;

typedef struct {
    const char *label; /* NULL draws a separator */
    const char *icon;
    action_t    action;
    const char *arg;
} popup_item_t;

typedef enum {
    POPUP_NONE,
    POPUP_MENU,
    POPUP_CALENDAR,
} popup_kind_t;

static menu_category_t menu_cats[MENU_MAX_CATS];
static size_t          menu_cat_count;
static menu_row_t      menu_rows[MENU_MAX_ROWS];
static size_t          menu_row_count;

static bool   menu_open;
static int    menu_index;
static int    menu_app_index = -1;
static bool   taskbar_dirty = true;
static bool   menu_dirty    = true;
static char   notification[96];
static rect_t notification_rect;
static uint64_t notification_until;
static uint32_t last_clock_second = 0xFFFFFFFF;
static desktop_wallpaper_mode_t wallpaper_mode = DESKTOP_WALLPAPER_DEFAULT;
static color_t wallpaper_top = COLOR_DESKTOP_TOP;
static color_t wallpaper_bottom = COLOR_DESKTOP_BOTTOM;

/* Desktop shortcuts: apps whose manifest says desktop=1. */
static const app_entry_t *desktop_icons[DESKTOP_MAX_ICONS];
static size_t   desktop_icon_count;
static int      desktop_selected = -1;
static int      desktop_last_click = -1;
static uint64_t desktop_last_click_time;

/* Context menu or calendar, drawn above everything but the cursor. */
static popup_kind_t popup_kind;
static popup_item_t popup_items[POPUP_MAX_ITEMS];
static size_t       popup_count;
static int          popup_hot = -1;
static rect_t       popup_rect;
static window_t    *popup_window;
static bool         popup_dirty;

/* Tooltips: the hover target is an opaque id, the text is built lazily. */
static int      hover_target;
static uint64_t hover_since;
static bool     tooltip_visible;
static char     tooltip_text[72];
static rect_t   tooltip_rect;

static void launch_path(const char *path, bool single_instance);

/* Mouse cursor */

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

/* Desktop icons */

static rect_t desktop_icon_rect(size_t index)
{
    rect_t space = wm_workspace();
    int rows = MAX(1, (space.h - 16) / DESKTOP_CELL_H);
    int col = (int)index / rows, row = (int)index % rows;
    return rect_make(10 + col * (DESKTOP_CELL_W + 6), 12 + row * DESKTOP_CELL_H,
                     DESKTOP_CELL_W, DESKTOP_CELL_H - 6);
}

static void build_desktop_icons(void)
{
    size_t count = 0;
    const app_entry_t *apps = apps_registry(&count);
    desktop_icon_count = 0;
    /* Keep a stable, task oriented order rather than the menu's. */
    static const char *const preferred[] = {
        PATH_FILES, PATH_TERMINAL, "/apps/gui/editor.elf", PATH_TASKMGR,
        PATH_SETTINGS,
    };
    for (size_t p = 0; p < ARRAY_SIZE(preferred); p++) {
        const app_entry_t *app = apps_find_path(preferred[p]);
        if (app && app->desktop && desktop_icon_count < DESKTOP_MAX_ICONS)
            desktop_icons[desktop_icon_count++] = app;
    }
    for (size_t i = 0; i < count && desktop_icon_count < DESKTOP_MAX_ICONS; i++) {
        bool listed = false;
        for (size_t j = 0; j < desktop_icon_count; j++)
            listed |= desktop_icons[j] == &apps[i];
        if (apps[i].desktop && !listed)
            desktop_icons[desktop_icon_count++] = &apps[i];
    }
}

static void shadow_text(int x, int y, const char *text, size_t n)
{
    gfx_text_n(x + 1, y + 1, text, n, RGB(0x08, 0x0b, 0x12));
    gfx_text_n(x, y, text, n, RGB(0xf2, 0xf4, 0xf8));
}

/* Labels wrap once at a space, then truncate to the cell width. */
static void draw_desktop_label(rect_t cell, const char *name, bool selected)
{
    size_t max = (size_t)(cell.w - 4) / FONT_W;
    size_t length = strlen(name);
    size_t first = length, second_start = length;
    if (length > max) {
        first = max;
        for (size_t i = max; i > 0; i--)
            if (name[i] == ' ') {
                first = i;
                break;
            }
        second_start = name[first] == ' ' ? first + 1 : first;
    }
    size_t second = MIN(length - second_start, max);
    int lines = second ? 2 : 1;
    int y = cell.y + 42;
    if (selected) {
        int w = (int)MAX(first, second) * FONT_W + 6;
        gfx_blend_rect(cell.x + (cell.w - w) / 2, y - 1, w, lines * FONT_H + 2,
                       COLOR_ACCENT_DARK, 220);
    }
    shadow_text(cell.x + (cell.w - (int)first * FONT_W) / 2, y, name, first);
    if (second)
        shadow_text(cell.x + (cell.w - (int)second * FONT_W) / 2, y + FONT_H,
                    name + second_start, second);
}

static void draw_desktop_icons(void)
{
    for (size_t i = 0; i < desktop_icon_count; i++) {
        rect_t cell = desktop_icon_rect(i);
        if (rect_empty(rect_intersect(cell, gfx_clip_current())))
            continue;
        bool selected = (int)i == desktop_selected;
        if (selected)
            gfx_blend_rect(cell.x + (cell.w - 40) / 2, cell.y + 2, 40, 38,
                           COLOR_ACCENT, 110);
        app_icon_draw(cell.x + (cell.w - 32) / 2, cell.y + 5, 32,
                      desktop_icons[i]);
        draw_desktop_label(cell, desktop_icons[i]->name, selected);
    }
}

static int desktop_icon_at(int x, int y)
{
    for (size_t i = 0; i < desktop_icon_count; i++)
        if (rect_contains(desktop_icon_rect(i), x, y))
            return (int)i;
    return -1;
}

static void desktop_select(int index)
{
    if (index == desktop_selected)
        return;
    if (desktop_selected >= 0)
        wm_expose_rect(desktop_icon_rect((size_t)desktop_selected));
    if (index >= 0)
        wm_expose_rect(desktop_icon_rect((size_t)index));
    desktop_selected = index;
}

/* Wallpaper */

/* A soft radial glow, blended in concentric bands so it fades towards the
 * edges. Drawn per scanline (one blend call each), which keeps it cheap
 * enough to repaint whenever a window uncovers part of the desktop. */
static void wallpaper_glow(int cx, int cy, int radius, color_t color,
                           uint8_t max_alpha)
{
    for (int dy = -radius; dy <= radius; dy++) {
        int span = 0;
        while (span * span + dy * dy <= radius * radius)
            span++;
        span--;
        if (span <= 0)
            continue;
        int dist    = dy < 0 ? -dy : dy;
        int alpha   = max_alpha * (radius - dist) / radius;
        gfx_blend_rect(cx - span, cy + dy, span * 2, 1, color, (uint8_t)alpha);
    }
}

static void draw_wallpaper_art(rect_t space)
{
    gfx_gradient_v(0, 0, space.w, space.h, COLOR_DESKTOP_TOP,
                   COLOR_DESKTOP_BOTTOM);

    /* A deliberately low-resolution wireframe: metallic planes and violet
     * construction lines read like an old workstation demo, not a modern
     * gradient wallpaper. */
    wallpaper_glow(space.w / 2, space.h / 2 - 80, 300, RGB(0x55, 0x63, 0x9e),
                   24);
    int vanishing_x = space.w / 2;
    int vanishing_y = space.h / 2 + 28;
    for (int x = -space.h; x < space.w + space.h; x += 48)
        gfx_vline(x, vanishing_y, space.h - vanishing_y,
                  RGB(0x28, 0x35, 0x50));
    for (int y = vanishing_y; y < space.h; y += 28)
        gfx_hline(0, y, space.w, RGB(0x27, 0x32, 0x4b));
    for (int i = 1; i < 7; i++) {
        int y = vanishing_y + i * i * 13;
        gfx_hline(0, y, space.w, RGB(0x34, 0x42, 0x61));
    }
    for (int i = 0; i < 5; i++) {
        int y = 70 + i * 34;
        gfx_hline(vanishing_x - 310 + i * 18, y, 620 - i * 36,
                  RGB(0x4e, 0x4e, 0x80));
    }

    /* Wordmark in the middle of the desktop. */
    int cx = space.w / 2;
    int cy = space.h / 2 - 40;
    widget_icon(cx - 24, cy - 60, 48, ICON_TINOS);

    const char *title = TINOS_NAME " " TINOS_VERSION;
    gfx_text(cx - gfx_text_width(title) / 2, cy, title, RGB(0xd5, 0xdb, 0xe7));

    const char *lines[] = {
        "Press Win or Ctrl+Esc (or click Start) to open the start menu",
        "Alt+Tab switches windows, Alt+F4 closes the focused one",
        "Win+D shows the desktop, Ctrl+Shift+Esc opens the Task Manager",
    };
    for (size_t i = 0; i < ARRAY_SIZE(lines); i++)
        gfx_text(cx - gfx_text_width(lines[i]) / 2, cy + 28 + (int)i * 20,
                 lines[i], RGB(0x9e, 0xaa, 0xc0));
}

/* Callers set the clip: the full workspace for a full repaint, or a single
 * exposed rect when repairing the area a moved/closed/minimised window
 * uncovered. */
static void draw_wallpaper(void)
{
    rect_t space = wm_workspace();

    if (wallpaper_mode == DESKTOP_WALLPAPER_SOLID)
        gfx_fill_rect(0, 0, space.w, space.h, wallpaper_top);
    else if (wallpaper_mode == DESKTOP_WALLPAPER_GRADIENT)
        gfx_gradient_v(0, 0, space.w, space.h, wallpaper_top,
                       wallpaper_bottom);
    else
        draw_wallpaper_art(space);
    draw_desktop_icons();
}

void desktop_set_wallpaper(desktop_wallpaper_mode_t mode, color_t top,
                           color_t bottom)
{
    wallpaper_mode = mode;
    wallpaper_top = top;
    wallpaper_bottom = bottom;
    bus_publish(&(bus_event_t){ BUS_WALLPAPER_CHANGED, mode, top, bottom });
    wm_invalidate_all();
}

desktop_wallpaper_mode_t desktop_wallpaper_mode(void)
{
    return wallpaper_mode;
}

/* Taskbar */

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
    int    avail = bar.w - start - TRAY_WIDTH - 8;
    size_t count = wm_count();
    if (count && (int)(count * (size_t)(width + 4)) > avail)
        width = MAX(70, avail / (int)count - 4);
    return rect_make(start + (int)index * (width + 4), bar.y + 4, width,
                     TASKBAR_HEIGHT - 8);
}

static bool task_button_visible(rect_t r)
{
    return r.x + r.w <= gfx_width() - TRAY_WIDTH;
}

static rect_t tray_rect(void)
{
    rect_t bar = taskbar_rect();
    return rect_make(bar.w - TRAY_WIDTH - 2, bar.y + 3, TRAY_WIDTH,
                     TASKBAR_HEIGHT - 6);
}

static rect_t tray_item_rect(tray_item_t item)
{
    static const int widths[TRAY_COUNT] = {
        TRAY_ICON_W, TRAY_ICON_W, TRAY_MEMORY_W, CLOCK_WIDTH,
    };
    rect_t tray = tray_rect();
    int x = tray.x + 5;
    for (int i = 0; i < (int)item; i++)
        x += widths[i] + TRAY_GAP;
    return rect_make(x, tray.y + 1, widths[item], tray.h - 2);
}

static bool network_online(void)
{
    return net_available() && net_config()->configured;
}

static void memory_usage(uint32_t *used_kib, uint32_t *total_kib)
{
    *total_kib = (uint32_t)(pmm_total_bytes() / 1024);
    *used_kib = (uint32_t)(pmm_used_bytes() / 1024);
}

static void format_clock(char *buf, size_t size)
{
    rtc_time_t now;
    rtc_read(&now);
    if (now.valid) {
        ksnprintf(buf, size, "%02u:%02u", now.hour, now.minute);
    } else {
        uint32_t s = (uint32_t)(timer_ticks() / 1000);
        ksnprintf(buf, size, "%02u:%02u", (s / 3600) % 100, (s / 60) % 60);
    }
}

static void draw_tray(void)
{
    rect_t tray = tray_rect();
    gfx_fill_rect(tray.x, tray.y, tray.w, tray.h, COLOR_TASKBAR);
    gfx_bevel(tray.x, tray.y, tray.w, tray.h, COLOR_BORDER, COLOR_PANEL_LIGHT);

    rect_t net = tray_item_rect(TRAY_NETWORK);
    widget_icon(net.x + 3, net.y + (net.h - 16) / 2, 16,
                network_online() ? ICON_NETWORK : ICON_NETWORK_OFF);

    rect_t vol = tray_item_rect(TRAY_VOLUME);
    widget_icon(vol.x + 3, vol.y + (vol.h - 16) / 2, 16,
                !audio_available() || audio_is_muted() ? ICON_VOLUME_MUTE
                                                       : ICON_VOLUME);

    rect_t mem = tray_item_rect(TRAY_MEMORY);
    uint32_t used, total;
    memory_usage(&used, &total);
    int gauge_w = mem.w - 8, gauge_h = 8;
    int gx = mem.x + 4, gy = mem.y + (mem.h - gauge_h) / 2;
    int fill = total ? (int)((uint64_t)used * (gauge_w - 2) / total) : 0;
    color_t level = used * 10 >= total * 9 ? COLOR_DANGER :
                    used * 4 >= total * 3 ? RGB(0xe0, 0xa0, 0x40) : COLOR_OK;
    gfx_fill_rect(gx, gy, gauge_w, gauge_h, RGB(0x45, 0x4e, 0x5d));
    gfx_fill_rect(gx + 1, gy + 1, CLAMP(fill, 1, gauge_w - 2), gauge_h - 2,
                  level);
    gfx_rect(gx, gy, gauge_w, gauge_h, COLOR_BORDER);

    char clock[16];
    format_clock(clock, sizeof(clock));
    rect_t c = tray_item_rect(TRAY_CLOCK);
    gfx_vline(c.x - 2, c.y + 2, c.h - 4, COLOR_BORDER);
    if (popup_kind == POPUP_CALENDAR)
        gfx_fill_rect(c.x, c.y, c.w, c.h, COLOR_PANEL_LIGHT);
    widget_text_centered(c, clock, COLOR_TEXT);
}

static void draw_taskbar(void)
{
    rect_t bar = taskbar_rect();

    gfx_clip_reset();
    gfx_clip_push(bar);
    gfx_fill_rect(bar.x, bar.y, bar.w, bar.h, COLOR_TASKBAR);
    gfx_bevel(bar.x, bar.y, bar.w, bar.h, COLOR_PANEL_LIGHT,
              COLOR_TASKBAR_EDGE);
    gfx_hline(bar.x, bar.y, bar.w, COLOR_TASKBAR_EDGE);

    /* Start button */
    rect_t start = start_button_rect();
    gfx_fill_rect(start.x, start.y, start.w, start.h,
                  menu_open ? COLOR_ACCENT_DARK : COLOR_PANEL);
    gfx_bevel(start.x, start.y, start.w, start.h,
              menu_open ? COLOR_BORDER : COLOR_PANEL_LIGHT,
              menu_open ? COLOR_PANEL_LIGHT : COLOR_BORDER);
    gfx_rect(start.x, start.y, start.w, start.h, COLOR_BORDER);
    widget_icon(start.x + 5, start.y + 2, 16, ICON_TINOS);
    gfx_text(start.x + 30, start.y + (start.h - FONT_H) / 2, "Start",
             menu_open ? RGB(0xf0, 0xf2, 0xf5) : COLOR_TEXT);

    /* One button per window, highlighting the focused one. */
    window_t *focused = wm_focused();
    size_t    index   = 0;
    for (window_t *w = wm_bottom(); w; w = w->above, index++) {
        rect_t r = task_button_rect(index);
        if (!task_button_visible(r))
            break;
        bool active = (w == focused) && !w->minimized;
        gfx_fill_rect(r.x, r.y, r.w, r.h,
                      active ? COLOR_PANEL_LIGHT : COLOR_TASKBAR);
        gfx_bevel(r.x, r.y, r.w, r.h, active ? COLOR_PANEL_LIGHT : COLOR_PANEL,
                  active ? COLOR_BORDER : COLOR_TASKBAR_EDGE);
        gfx_rect(r.x, r.y, r.w, r.h, active ? COLOR_ACCENT : COLOR_BORDER);
        if (active)
            gfx_hline(r.x + 1, r.y + r.h - 2, r.w - 2, COLOR_ACCENT);
        if (!w->icon_name ||
            !widget_icon_name(r.x + 4, r.y + 2, 16, w->icon_name))
            widget_icon(r.x + 4, r.y + 2, 16, ICON_APP);

        gfx_clip_push(rect_make(r.x + 24, r.y, r.w - 28, r.h));
        gfx_text(r.x + 26, r.y + (r.h - FONT_H) / 2, w->title,
                 w->minimized ? COLOR_TEXT_DIM : COLOR_TEXT);
        gfx_clip_pop();
    }

    draw_tray();
    gfx_clip_pop();
    taskbar_dirty = false;
}

/* Start menu */

static void menu_rebuild_rows(void);

/* Group the registered applications into categories, in first-seen order,
 * and select the first category so its submenu is immediately visible. */
static void build_menu(void)
{
    size_t             app_count = 0;
    const app_entry_t *apps      = apps_registry(&app_count);

    menu_cat_count = 0;
    for (size_t i = 0; i < app_count; i++) {
        const char *name = apps[i].category ? apps[i].category : "Other";
        size_t      c;
        for (c = 0; c < menu_cat_count; c++)
            if (strcmp(menu_cats[c].name, name) == 0)
                break;
        if (c < menu_cat_count || menu_cat_count >= MENU_MAX_CATS)
            continue;
        menu_cats[menu_cat_count] =
            (menu_category_t){ name, ICON_MENU_FOLDER };
        menu_cat_count++;
    }
    menu_rebuild_rows();
}

/* The n-th application registered under category `cat`, or SIZE_MAX. */
static size_t cat_app_at(size_t cat, size_t n)
{
    size_t             app_count = 0;
    const app_entry_t *apps      = apps_registry(&app_count);
    const char        *name      = menu_cats[cat].name;

    for (size_t i = 0; i < app_count; i++) {
        const char *app_cat = apps[i].category ? apps[i].category : "Other";
        if (strcmp(app_cat, name) == 0 && n-- == 0)
            return i;
    }
    return SIZE_MAX;
}

/* Rebuild the flat list of visible rows: the categories, then the system
 * actions at the bottom. Applications are drawn in the right-hand submenu
 * instead of being inserted into this list. */
static void menu_rebuild_rows(void)
{
    menu_row_count = 0;
    for (size_t c = 0; c < menu_cat_count; c++) {
        menu_rows[menu_row_count++] = (menu_row_t){ ROW_CATEGORY, c, NULL };
    }
    menu_rows[menu_row_count++] = (menu_row_t){ ROW_SEPARATOR, 0, NULL };
    if (apps_find_path(PATH_TASKMGR))
        menu_rows[menu_row_count++] = (menu_row_t){ ROW_TASKMGR, 0, NULL };
    menu_rows[menu_row_count++] = (menu_row_t){ ROW_RESTART, 0, NULL };
    menu_rows[menu_row_count++] = (menu_row_t){ ROW_SHUTDOWN, 0, NULL };
}

static int menu_row_height(const menu_row_t *row)
{
    switch (row->kind) {
    case ROW_CATEGORY:
        return MENU_CAT_H;
    case ROW_SEPARATOR:
        return 9;
    default:
        return MENU_ITEM_H;
    }
}

static int menu_height(void)
{
    int height = MENU_HEADER_H + 8;
    for (size_t i = 0; i < menu_row_count; i++)
        height += menu_row_height(&menu_rows[i]);
    int app_count = 0;
    for (size_t c = 0; c < menu_cat_count; c++) {
        int count = 0;
        for (size_t n = 0; cat_app_at(c, n) != SIZE_MAX; n++)
            count++;
        if (count > app_count)
            app_count = count;
    }
    int submenu_height = MENU_HEADER_H + 6 + app_count * MENU_ITEM_H + 8;
    if (submenu_height > height)
        height = submenu_height;
    return height + 6;
}

static rect_t menu_rect(void)
{
    int h = menu_height();
    return rect_make(4, gfx_height() - TASKBAR_HEIGHT - h,
                     MENU_WIDTH + MENU_SUBMENU_W, h);
}

/* The menu frame is kept exact so dismissing it repaints only its bounds. */
static rect_t menu_frame_rect(void)
{
    return menu_rect();
}

static rect_t menu_item_rect(size_t index)
{
    rect_t r = menu_rect();
    int    y = r.y + MENU_HEADER_H + 6;
    for (size_t i = 0; i < index && i < menu_row_count; i++)
        y += menu_row_height(&menu_rows[i]);
    return rect_make(r.x + 4, y, MENU_WIDTH - 8,
                     menu_row_height(&menu_rows[index]));
}

static rect_t menu_app_rect(size_t index)
{
    rect_t r = menu_rect();
    return rect_make(r.x + MENU_WIDTH + 4,
                     r.y + MENU_HEADER_H + 6 + (int)index * MENU_ITEM_H,
                     MENU_SUBMENU_W - 8, MENU_ITEM_H);
}

static void draw_menu_row(size_t i, rect_t item, bool selected)
{
    const menu_row_t *row = &menu_rows[i];

    if (row->kind == ROW_CATEGORY) {
        if (selected) {
            gfx_fill_rect(item.x, item.y, item.w, item.h, COLOR_ACCENT_DARK);
            gfx_rect(item.x, item.y, item.w, item.h, COLOR_ACCENT);
        }
        widget_icon(item.x + 8, item.y + (item.h - 16) / 2, 16,
                    menu_cats[row->cat].icon);
        gfx_char(item.x + item.w - 16, item.y + (item.h - FONT_H) / 2,
                 '\x10', selected ? COLOR_TEXT : COLOR_ACCENT);
        gfx_text(item.x + 34, item.y + (item.h - FONT_H) / 2,
                 menu_cats[row->cat].name,
                 selected ? RGB(0xff, 0xff, 0xff) : COLOR_TEXT);
        return;
    }

    const char *label;
    bool        indented = row->kind == ROW_APP;

    if (selected) {
        gfx_fill_rect(item.x, item.y, item.w, item.h, COLOR_ACCENT_DARK);
        gfx_rect(item.x, item.y, item.w, item.h, COLOR_ACCENT);
    }
    int ix = item.x + (indented ? 24 : 8);
    int tx = item.x + (indented ? 50 : 34);
    if (row->kind == ROW_APP) {
        label = row->app->name;
        app_icon_draw(ix, item.y + (item.h - 16) / 2, 16, row->app);
    } else {
        icon_t icon = row->kind == ROW_TASKMGR ? ICON_TASKMGR :
                      row->kind == ROW_RESTART ? ICON_RESTART : ICON_POWER;
        label = row->kind == ROW_TASKMGR ? "Task Manager" :
                row->kind == ROW_RESTART ? "Restart" : "Shut Down";
        widget_icon(ix, item.y + (item.h - 16) / 2, 16, icon);
    }
    gfx_text(tx, item.y + (item.h - FONT_H) / 2, label,
             selected ? RGB(0xff, 0xff, 0xff) : COLOR_TEXT);
}

static void draw_menu_submenu(void)
{
    if (menu_index < 0 || menu_index >= (int)menu_row_count ||
        menu_rows[menu_index].kind != ROW_CATEGORY)
        return;

    size_t cat = menu_rows[menu_index].cat;
    size_t app_count = 0;
    const app_entry_t *apps = apps_registry(&app_count);
    size_t n = 0;
    for (;;) {
        size_t app_i = cat_app_at(cat, n);
        if (app_i == SIZE_MAX)
            break;
        rect_t item = menu_app_rect(n);
        const app_entry_t *app = &apps[app_i];
        bool selected = (int)n == menu_app_index;
        if (selected) {
            gfx_fill_rect(item.x, item.y, item.w, item.h, COLOR_ACCENT_DARK);
            gfx_rect(item.x, item.y, item.w, item.h, COLOR_ACCENT);
        }
        app_icon_draw(item.x + 8, item.y + (item.h - 16) / 2, 16, app);
        gfx_text(item.x + 34, item.y + (item.h - FONT_H) / 2, app->name,
                 selected ? RGB(0xff, 0xff, 0xff) : COLOR_TEXT);
        n++;
    }
}

static void draw_menu(void)
{
    if (!menu_open)
        return;

    rect_t r = menu_rect();
    gfx_clip_reset();

    gfx_clip_push(r);
    widget_panel(r, COLOR_PANEL, COLOR_BORDER);
    gfx_gradient_v(r.x + 2, r.y + 2, r.w - 4, MENU_HEADER_H - 4,
                   COLOR_TITLE_ACTIVE, COLOR_ACCENT_DARK);
    widget_icon(r.x + 8, r.y + 3, 32, ICON_USER);
    gfx_text(r.x + 48, r.y + 4, TINOS_DEFAULT_USER,
             RGB(0xf2, 0xf4, 0xf7));
    gfx_text(r.x + 48, r.y + 19, TINOS_NAME " " TINOS_VERSION,
             RGB(0xc8, 0xcf, 0xe6));
    gfx_vline(r.x + MENU_WIDTH, r.y + MENU_HEADER_H + 2,
              r.h - MENU_HEADER_H - 8, COLOR_BORDER);

    for (size_t i = 0; i < menu_row_count; i++) {
        rect_t item = menu_item_rect(i);
        if (menu_rows[i].kind == ROW_SEPARATOR) {
            gfx_hline(item.x + 6, item.y + 4, item.w - 12, COLOR_BORDER);
            continue;
        }
        draw_menu_row(i, item, (int)i == menu_index);
    }
    draw_menu_submenu();

    gfx_clip_pop();
    menu_dirty = false;
}

static void tooltip_hide(void);
static void popup_close(void);

static void menu_toggle(void)
{
    menu_open = !menu_open;
    if (menu_open) {
        popup_close();
        tooltip_hide();
        menu_index = 0;
        menu_app_index = -1;
        menu_dirty = true;
    } else {
        wm_expose_rect(menu_frame_rect());
    }
    taskbar_dirty = true;
}

static void menu_move(int delta)
{
    if (menu_row_count == 0)
        return;
    int index = menu_index;
    for (int guard = 0; guard < (int)menu_row_count * 2; guard++) {
        index += delta;
        if (index < 0)
            index = (int)menu_row_count - 1;
        if (index >= (int)menu_row_count)
            index = 0;
        if (menu_rows[index].kind != ROW_SEPARATOR)
            break;
    }
    menu_index = index;
    menu_app_index = -1;
    menu_dirty = true;
}

static int menu_app_count(size_t cat)
{
    int count = 0;
    for (size_t n = 0; cat_app_at(cat, n) != SIZE_MAX; n++)
        count++;
    return count;
}

static void menu_move_app(int delta)
{
    if (menu_index < 0 || menu_index >= (int)menu_row_count ||
        menu_rows[menu_index].kind != ROW_CATEGORY)
        return;
    int count = menu_app_count(menu_rows[menu_index].cat);
    if (count == 0)
        return;
    int index = menu_app_index + delta;
    if (index < 0)
        index = count - 1;
    if (index >= count)
        index = 0;
    menu_app_index = index;
    menu_dirty = true;
}

/* Launching */

/* The window belonging to a running instance of `path`, or NULL. */
static window_t *window_for_path(const char *path)
{
    for (window_t *w = wm_top(); w; w = w->below) {
        const char *running = process_path(desktop_user_window_pid(w));
        if (running && strcmp(running, path) == 0)
            return w;
    }
    return NULL;
}

/* Tools such as the Task Manager and Settings are single instance: asking
 * for them again just brings the open window forward. */
static void launch_path(const char *path, bool single_instance)
{
    const app_entry_t *app = apps_find_path(path);
    const char *name = app ? app->name : path;
    if (single_instance) {
        window_t *existing = window_for_path(path);
        if (existing) {
            wm_focus(existing);
            taskbar_dirty = true;
            return;
        }
    }
    fs_node_t *file;
    if (!path || fs_resolve(fs_root(), path, &file) != FS_OK ||
        file->type != FS_FILE || !file->data) {
        desktop_notify("%s is not installed", name);
        return;
    }
    const char *argv[] = { path };
    process_info_t list[PROCESS_MAX];
    size_t live = 0, count = process_list(list, PROCESS_MAX);
    for (size_t i = 0; i < count; i++)
        live += list[i].state != PROCESS_EXITED;
    int pid = process_spawn(file->data, fs_size(file), 1, argv);
    if (pid >= 0)
        desktop_notify("Starting %s", name);
    else if (live >= PROCESS_MAX)
        desktop_notify("Cannot start %s: all %u task slots are busy", name,
                       (uint32_t)PROCESS_MAX);
    else
        desktop_notify("Cannot start %s: not enough memory", name);
}

static bool single_instance_path(const char *path)
{
    return strcmp(path, PATH_TASKMGR) == 0 ||
           strcmp(path, PATH_SETTINGS) == 0 ||
           strcmp(path, PATH_ABOUT) == 0 ||
           strcmp(path, PATH_SYSINFO) == 0;
}

static void launch_program(const app_entry_t *app)
{
    if (!app || !app->path) {
        desktop_notify("Executable unavailable");
        return;
    }
    launch_path(app->path, single_instance_path(app->path));
}

static void menu_close_and_launch(const app_entry_t *app)
{
    menu_open = false;
    wm_expose_rect(menu_frame_rect());
    taskbar_dirty = true;
    launch_program(app);
}

static void menu_activate(size_t index)
{
    if (index >= menu_row_count)
        return;
    menu_row_t *row = &menu_rows[index];

    switch (row->kind) {
    case ROW_APP:
        menu_close_and_launch(row->app);
        break;
    case ROW_CATEGORY:
        menu_app_index = 0;
        menu_dirty = true;
        break;
    case ROW_TASKMGR:
        menu_close_and_launch(apps_find_path(PATH_TASKMGR));
        break;
    case ROW_RESTART:
        desktop_shutdown(true);
        break;
    case ROW_SHUTDOWN:
        desktop_shutdown(false);
        break;
    case ROW_SEPARATOR:
        break;
    }
}

/* Notifications */

/* desktop_notify() itself is variadic and so cannot be forwarded through a
 * plain function pointer; native apps format locally (they link printf.c
 * directly) and reach the notification banner through this instead. */
void desktop_notify_str(const char *text)
{
    if (notification[0])
        wm_expose_rect(notification_rect);
    strlcpy(notification, text, sizeof(notification));
    notification_until = timer_ticks() + NOTIFY_MS;
    int width = gfx_text_width(notification) + 48;
    notification_rect = rect_make((gfx_width() - width) / 2,
                                  gfx_height() - TASKBAR_HEIGHT - 52, width, 32);
    wm_expose_rect(notification_rect);
    klog("[note] %s\n", notification);
}

void desktop_notify(const char *fmt, ...)
{
    char    buf[sizeof(notification)];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    desktop_notify_str(buf);
}

static void draw_notification(void)
{
    if (!notification[0])
        return;
    rect_t r = notification_rect;
    gfx_clip_reset();
    widget_panel(r, COLOR_PANEL_LIGHT, COLOR_ACCENT);
    widget_icon(r.x + 8, r.y + 8, 16, ICON_INFO);
    widget_text_centered(rect_make(r.x + 24, r.y, r.w - 32, r.h),
                         notification, COLOR_TEXT);
}

/* Popups: context menus and the calendar */

static void popup_close(void)
{
    if (popup_kind == POPUP_NONE)
        return;
    bool calendar = popup_kind == POPUP_CALENDAR;
    popup_kind = POPUP_NONE;
    popup_window = NULL;
    popup_hot = -1;
    wm_expose_rect(popup_rect);
    if (calendar)
        taskbar_dirty = true;
}

static void popup_add(const char *label, const char *icon, action_t action,
                      const char *arg)
{
    if (popup_count < POPUP_MAX_ITEMS)
        popup_items[popup_count++] = (popup_item_t){ label, icon, action, arg };
}

static int popup_item_height(size_t i)
{
    return popup_items[i].label ? POPUP_ITEM_H : 9;
}

/* Lay the collected items out at (x, y), flipping upwards/leftwards so the
 * menu never leaves the screen or covers the taskbar. */
static void popup_show_menu(int x, int y)
{
    int width = 150, height = 8;
    for (size_t i = 0; i < popup_count; i++) {
        if (popup_items[i].label)
            width = MAX(width, gfx_text_width(popup_items[i].label) + 48);
        height += popup_item_height(i);
    }
    rect_t space = wm_workspace();
    if (x + width > gfx_width())
        x = MAX(0, x - width);
    if (y + height > space.h)
        y = MAX(0, y - height);
    popup_rect = rect_make(x, y, width, height);
    popup_kind = POPUP_MENU;
    popup_hot = -1;
    popup_dirty = true;
}

static rect_t popup_item_rect(size_t index)
{
    int y = popup_rect.y + 4;
    for (size_t i = 0; i < index; i++)
        y += popup_item_height(i);
    return rect_make(popup_rect.x + 3, y, popup_rect.w - 6,
                     popup_item_height(index));
}

static void open_desktop_menu(int x, int y)
{
    popup_close();
    popup_count = 0;
    popup_add("Terminal", "terminal", ACT_LAUNCH, PATH_TERMINAL);
    popup_add("Files", "folder", ACT_LAUNCH, PATH_FILES);
    popup_add(NULL, NULL, ACT_NONE, NULL);
    popup_add("Task Manager", "taskmgr", ACT_LAUNCH, PATH_TASKMGR);
    popup_add("Settings", "settings", ACT_LAUNCH, PATH_SETTINGS);
    popup_add(NULL, NULL, ACT_NONE, NULL);
    popup_add("Show desktop", "desktop", ACT_SHOW_DESKTOP, NULL);
    popup_add("About TinOS", "info", ACT_LAUNCH, PATH_ABOUT);
    popup_show_menu(x, y);
}

static void open_icon_menu(int x, int y, const app_entry_t *app)
{
    popup_close();
    popup_count = 0;
    popup_add("Open", app->icon_name, ACT_LAUNCH, app->path);
    popup_add(NULL, NULL, ACT_NONE, NULL);
    popup_add("Task Manager", "taskmgr", ACT_LAUNCH, PATH_TASKMGR);
    popup_show_menu(x, y);
}

static void open_window_menu(int x, int y, window_t *win)
{
    popup_close();
    popup_count = 0;
    if (win->minimized)
        popup_add("Restore", "up", ACT_WIN_RESTORE, NULL);
    else
        popup_add("Minimize", "down", ACT_WIN_MINIMIZE, NULL);
    popup_add(win->maximized ? "Restore size" : "Maximize", "desktop",
              ACT_WIN_MAXIMIZE, NULL);
    popup_add(NULL, NULL, ACT_NONE, NULL);
    popup_add("Close", "close", ACT_WIN_CLOSE, NULL);
    popup_show_menu(x, y);
    popup_window = win;
}

static void open_tray_menu(int x, int y)
{
    popup_close();
    popup_count = 0;
    popup_add(audio_is_muted() ? "Unmute sound" : "Mute sound",
              audio_is_muted() ? "volume" : "volume_mute", ACT_TOGGLE_MUTE,
              NULL);
    popup_add("Settings", "settings", ACT_LAUNCH, PATH_SETTINGS);
    popup_add("Task Manager", "taskmgr", ACT_LAUNCH, PATH_TASKMGR);
    popup_show_menu(x, y);
}

static void open_calendar(void)
{
    bool was_open = popup_kind == POPUP_CALENDAR;
    popup_close();
    if (was_open)
        return;
    rect_t clock = tray_item_rect(TRAY_CLOCK);
    int w = 7 * 28 + 24, h = 7 * 20 + 70;
    popup_rect = rect_make(MIN(clock.x + clock.w, gfx_width() - 4) - w,
                           gfx_height() - TASKBAR_HEIGHT - h - 2, w, h);
    popup_kind = POPUP_CALENDAR;
    popup_dirty = true;
    taskbar_dirty = true;
}

static bool window_alive(window_t *target)
{
    for (window_t *w = wm_bottom(); w; w = w->above)
        if (w == target)
            return true;
    return false;
}

static void run_action(action_t action, const char *arg, window_t *win)
{
    switch (action) {
    case ACT_LAUNCH:
        launch_path(arg, single_instance_path(arg));
        break;
    case ACT_SHOW_DESKTOP:
        wm_show_desktop();
        break;
    case ACT_WIN_RESTORE:
        if (win)
            wm_focus(win);
        break;
    case ACT_WIN_MINIMIZE:
        if (win && !win->minimized)
            wm_toggle_minimize(win);
        break;
    case ACT_WIN_MAXIMIZE:
        if (win) {
            wm_focus(win);
            wm_toggle_maximize(win);
        }
        break;
    case ACT_WIN_CLOSE:
        if (win)
            wm_close(win);
        break;
    case ACT_TOGGLE_MUTE:
        audio_set_muted(!audio_is_muted());
        break;
    case ACT_RESTART:
        desktop_shutdown(true);
        break;
    case ACT_SHUTDOWN:
        desktop_shutdown(false);
        break;
    case ACT_NONE:
        break;
    }
    taskbar_dirty = true;
}

static void popup_activate(int index)
{
    if (popup_kind != POPUP_MENU || index < 0 || index >= (int)popup_count ||
        !popup_items[index].label)
        return;
    popup_item_t item = popup_items[index];
    window_t *win = popup_window && window_alive(popup_window) ? popup_window
                                                               : NULL;
    popup_close();
    run_action(item.action, item.arg, win);
}

static void popup_move(int delta)
{
    if (!popup_count)
        return;
    int index = popup_hot;
    for (size_t guard = 0; guard < popup_count; guard++) {
        index = (index + delta + (int)popup_count) % (int)popup_count;
        if (popup_items[index].label)
            break;
    }
    popup_hot = index;
    popup_dirty = true;
}

/* Sakamoto's day-of-week, 0 = Sunday. */
static int weekday(int y, int m, int d)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3)
        y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static int days_in_month(int y, int m)
{
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return m == 2 && leap ? 29 : days[m - 1];
}

static void draw_calendar(rect_t r)
{
    static const char *const months[] = {
        "January", "February", "March", "April", "May", "June", "July",
        "August", "September", "October", "November", "December",
    };
    static const char *const days[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
        "Saturday",
    };
    widget_panel(r, COLOR_PANEL_LIGHT, COLOR_BORDER);
    gfx_gradient_v(r.x + 2, r.y + 2, r.w - 4, 40, COLOR_TITLE_ACTIVE,
                   COLOR_ACCENT_DARK);
    widget_icon(r.x + 8, r.y + 6, 32, ICON_CALENDAR);
    rtc_time_t now;
    rtc_read(&now);
    char line[48];
    if (!now.valid || now.month < 1 || now.month > 12) {
        gfx_text(r.x + 48, r.y + 14, "Clock unavailable", COLOR_PANEL_LIGHT);
        return;
    }
    int wd = weekday(now.year, now.month, now.day);
    gfx_text(r.x + 48, r.y + 5, days[wd], RGB(0xf2, 0xf4, 0xf7));
    ksnprintf(line, sizeof(line), "%u %s %u", now.day, months[now.month - 1],
              now.year);
    gfx_text(r.x + 48, r.y + 22, line, RGB(0xc8, 0xcf, 0xe6));

    int gx = r.x + 12, gy = r.y + 50;
    static const char *const heads[] = { "Su", "Mo", "Tu", "We", "Th", "Fr",
                                         "Sa" };
    for (int i = 0; i < 7; i++)
        gfx_text(gx + i * 28 + 6, gy, heads[i],
                 i == 0 || i == 6 ? COLOR_DANGER : COLOR_TEXT_DIM);
    gfx_hline(gx, gy + 18, 7 * 28, COLOR_BORDER);
    int first = weekday(now.year, now.month, 1);
    int count = days_in_month(now.year, now.month);
    for (int d = 1; d <= count; d++) {
        int cell = first + d - 1;
        int cx = gx + (cell % 7) * 28, cy = gy + 22 + (cell / 7) * 20;
        ksnprintf(line, sizeof(line), "%2d", d);
        if (d == now.day) {
            gfx_fill_rect(cx + 2, cy - 1, 24, 18, COLOR_ACCENT_DARK);
            gfx_text(cx + 6, cy, line, RGB(0xff, 0xff, 0xff));
        } else {
            gfx_text(cx + 6, cy, line, COLOR_TEXT);
        }
    }
    uint32_t s = (uint32_t)(timer_ticks() / 1000);
    ksnprintf(line, sizeof(line), "%02u:%02u:%02u  up %uh %02um", now.hour,
              now.minute, now.second, s / 3600, (s / 60) % 60);
    gfx_text(r.x + 12, r.y + r.h - 20, line, COLOR_TEXT_DIM);
}

static void draw_popup(void)
{
    if (popup_kind == POPUP_NONE)
        return;
    rect_t r = popup_rect;
    gfx_clip_reset();
    gfx_clip_push(r);
    if (popup_kind == POPUP_CALENDAR) {
        draw_calendar(r);
    } else {
        widget_panel(r, COLOR_PANEL, COLOR_BORDER);
        for (size_t i = 0; i < popup_count; i++) {
            rect_t item = popup_item_rect(i);
            if (!popup_items[i].label) {
                gfx_hline(item.x + 4, item.y + 4, item.w - 8, COLOR_BORDER);
                continue;
            }
            bool hot = (int)i == popup_hot;
            if (hot) {
                gfx_fill_rect(item.x, item.y, item.w, item.h, COLOR_ACCENT_DARK);
                gfx_rect(item.x, item.y, item.w, item.h, COLOR_ACCENT);
            }
            if (popup_items[i].icon)
                widget_icon_name(item.x + 6, item.y + (item.h - 16) / 2, 16,
                                 popup_items[i].icon);
            gfx_text(item.x + 30, item.y + (item.h - FONT_H) / 2,
                     popup_items[i].label,
                     hot ? RGB(0xff, 0xff, 0xff) : COLOR_TEXT);
        }
    }
    gfx_clip_pop();
    popup_dirty = false;
}

/* Tooltips */

enum {
    HOVER_NONE = 0,
    HOVER_TRAY = 1,     /* + tray_item_t */
    HOVER_TASK = 100,   /* + task button index */
    HOVER_ICON = 200,   /* + desktop icon index */
};

static int hover_target_at(int x, int y)
{
    if (menu_open || popup_kind != POPUP_NONE)
        return HOVER_NONE;
    if (rect_contains(taskbar_rect(), x, y)) {
        for (int i = 0; i < TRAY_COUNT; i++)
            if (rect_contains(tray_item_rect((tray_item_t)i), x, y))
                return HOVER_TRAY + i;
        size_t index = 0;
        for (window_t *w = wm_bottom(); w; w = w->above, index++)
            if (rect_contains(task_button_rect(index), x, y))
                return HOVER_TASK + (int)index;
        return HOVER_NONE;
    }
    rect_t space = wm_workspace();
    for (window_t *w = wm_top(); w; w = w->below)
        if (!w->minimized && rect_contains(w->frame, x, y))
            return HOVER_NONE;
    int icon = rect_contains(space, x, y) ? desktop_icon_at(x, y) : -1;
    return icon >= 0 ? HOVER_ICON + icon : HOVER_NONE;
}

static bool tooltip_build(int target)
{
    if (target >= HOVER_ICON) {
        size_t i = (size_t)(target - HOVER_ICON);
        if (i >= desktop_icon_count)
            return false;
        const app_entry_t *app = desktop_icons[i];
        ksnprintf(tooltip_text, sizeof(tooltip_text), "%s",
                  app->summary && app->summary[0] ? app->summary : app->name);
        return true;
    }
    if (target >= HOVER_TASK) {
        size_t index = 0;
        for (window_t *w = wm_bottom(); w; w = w->above, index++)
            if ((int)index == target - HOVER_TASK) {
                strlcpy(tooltip_text, w->title, sizeof(tooltip_text));
                return true;
            }
        return false;
    }
    switch (target - HOVER_TRAY) {
    case TRAY_NETWORK:
        if (!net_available()) {
            strlcpy(tooltip_text, "No network adapter", sizeof(tooltip_text));
        } else if (!net_config()->configured) {
            strlcpy(tooltip_text, "Network: not connected",
                    sizeof(tooltip_text));
        } else {
            char address[20];
            net_format_address(net_config()->address, address, sizeof(address));
            ksnprintf(tooltip_text, sizeof(tooltip_text), "Network: %s",
                      address);
        }
        return true;
    case TRAY_VOLUME:
        if (!audio_available())
            strlcpy(tooltip_text, "No sound device", sizeof(tooltip_text));
        else
            ksnprintf(tooltip_text, sizeof(tooltip_text),
                      "Volume %u%%%s (wheel adjusts, click mutes)",
                      audio_get_volume(), audio_is_muted() ? ", muted" : "");
        return true;
    case TRAY_MEMORY: {
        uint32_t used, total;
        memory_usage(&used, &total);
        ksnprintf(tooltip_text, sizeof(tooltip_text),
                  "Memory: %u of %u KiB in use", used, total);
        return true;
    }
    case TRAY_CLOCK: {
        rtc_time_t now;
        rtc_read(&now);
        if (now.valid)
            ksnprintf(tooltip_text, sizeof(tooltip_text),
                      "%04u-%02u-%02u %02u:%02u:%02u", now.year, now.month,
                      now.day, now.hour, now.minute, now.second);
        else
            strlcpy(tooltip_text, "Uptime", sizeof(tooltip_text));
        return true;
    }
    default:
        return false;
    }
}

/* Hiding also disarms the hover timer: a tooltip only comes back after the
 * pointer moves onto something again. */
static void tooltip_hide(void)
{
    hover_target = HOVER_NONE;
    if (!tooltip_visible)
        return;
    tooltip_visible = false;
    wm_expose_rect(tooltip_rect);
}

static void tooltip_show(int target, int mx, int my)
{
    if (!tooltip_build(target))
        return;
    int w = gfx_text_width(tooltip_text) + 12, h = FONT_H + 6;
    int x = CLAMP(mx - w / 2, 2, gfx_width() - w - 2);
    int y = target < HOVER_ICON ? gfx_height() - TASKBAR_HEIGHT - h - 4
                                : my + 22;
    tooltip_rect = rect_make(x, y, w, h);
    tooltip_visible = true;
    wm_expose_rect(tooltip_rect);
}

static void draw_tooltip(void)
{
    if (!tooltip_visible)
        return;
    gfx_clip_reset();
    rect_t r = tooltip_rect;
    gfx_fill_rect(r.x, r.y, r.w, r.h, RGB(0xff, 0xf8, 0xd8));
    gfx_rect(r.x, r.y, r.w, r.h, COLOR_TEXT_DARK);
    gfx_text(r.x + 6, r.y + 3, tooltip_text, COLOR_TEXT_DARK);
}

static void track_hover(int x, int y)
{
    int target = hover_target_at(x, y);
    if (target == hover_target)
        return;
    tooltip_hide();
    hover_target = target;
    hover_since = timer_ticks();
}

/* Shutdown */

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

/* Input handling */

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

/* System shortcuts that work no matter which window has focus. */
static bool handle_shortcut(const key_event_t *ev)
{
    bool super = ev->mods & KMOD_SUPER;
    bool ctrl_alt = (ev->mods & (KMOD_CTRL | KMOD_ALT)) ==
                    (KMOD_CTRL | KMOD_ALT);
    int key = lower(ev->code);

    if ((ev->mods & KMOD_CTRL) && (ev->mods & KMOD_SHIFT) &&
        ev->code == KEY_ESCAPE) {
        launch_path(PATH_TASKMGR, true);
        return true;
    }
    if ((super || ctrl_alt) && key == 'd') {
        popup_close();
        wm_show_desktop();
        taskbar_dirty = true;
        return true;
    }
    if ((super && key == 'e') || (ctrl_alt && key == 'f')) {
        launch_path(PATH_FILES, false);
        return true;
    }
    if ((super && key == 't') || (ctrl_alt && key == 't')) {
        launch_path(PATH_TERMINAL, false);
        return true;
    }
    if (super && key == 'i') {
        launch_path(PATH_SETTINGS, true);
        return true;
    }
    if (super && (ev->code == KEY_LEFT || ev->code == KEY_RIGHT)) {
        wm_snap(wm_focused(), ev->code == KEY_LEFT ? -1 : 1);
        return true;
    }
    if (super && ev->code == KEY_UP) {
        wm_maximize(wm_focused());
        return true;
    }
    if (super && ev->code == KEY_DOWN) {
        window_t *w = wm_focused();
        if (w && w->maximized)
            wm_toggle_maximize(w);
        else if (w)
            wm_toggle_minimize(w);
        taskbar_dirty = true;
        return true;
    }
    return false;
}

static bool handle_global_key(const key_event_t *ev)
{
    tooltip_hide();

    if (popup_kind == POPUP_MENU) {
        switch (ev->code) {
        case KEY_ESCAPE:
            popup_close();
            return true;
        case KEY_UP:
            popup_move(-1);
            return true;
        case KEY_DOWN:
            popup_move(1);
            return true;
        case KEY_ENTER:
            popup_activate(popup_hot);
            return true;
        default:
            break;
        }
    } else if (popup_kind == POPUP_CALENDAR && ev->code == KEY_ESCAPE) {
        popup_close();
        return true;
    }

    /* Start menu navigation swallows keys while it is open. */
    if (menu_open) {
        switch (ev->code) {
        case KEY_ESCAPE:
            menu_toggle();
            return true;
        case KEY_UP:
            if (menu_app_index >= 0)
                menu_move_app(-1);
            else
                menu_move(-1);
            return true;
        case KEY_DOWN:
            if (menu_app_index >= 0)
                menu_move_app(1);
            else
                menu_move(1);
            return true;
        case KEY_ENTER:
            if (menu_app_index >= 0 &&
                menu_rows[menu_index].kind == ROW_CATEGORY) {
                size_t app_count = 0;
                const app_entry_t *apps = apps_registry(&app_count);
                size_t app_i = cat_app_at(menu_rows[menu_index].cat,
                                          (size_t)menu_app_index);
                if (app_i != SIZE_MAX)
                    menu_close_and_launch(&apps[app_i]);
            } else {
                menu_activate((size_t)menu_index);
            }
            return true;
        case KEY_RIGHT:
            if (menu_index >= 0 && menu_index < (int)menu_row_count &&
                menu_rows[menu_index].kind == ROW_CATEGORY)
                menu_app_index = menu_app_count(menu_rows[menu_index].cat) ? 0 : -1;
            menu_dirty = true;
            return true;
        case KEY_LEFT:
            menu_app_index = -1;
            menu_dirty = true;
            return true;
        default:
            break;
        }
    }

    if (handle_shortcut(ev))
        return true;
    if ((ev->mods & KMOD_CTRL) && ev->code == KEY_ESCAPE) {
        menu_toggle();
        return true;
    }
    if (ev->code == KEY_SUPER) {
        menu_toggle();
        return true;
    }
    if (ev->code == KEY_F12) {
        menu_toggle();
        return true;
    }
    if ((ev->mods & KMOD_ALT) && ev->code == KEY_TAB) {
        wm_cycle_focus();
        taskbar_dirty = true;
        return true;
    }
    if ((ev->mods & KMOD_ALT) && ev->code == KEY_F4) {
        window_t *focused = wm_focused();
        if (focused)
            wm_close(focused);
        taskbar_dirty = true;
        return true;
    }
    /* With no window focused, Enter opens the selected desktop icon. */
    if (!wm_focused() && desktop_selected >= 0 && ev->code == KEY_ENTER) {
        launch_program(desktop_icons[desktop_selected]);
        return true;
    }
    return false;
}

static void handle_tray_click(const mouse_event_t *ev, tray_item_t item)
{
    if (ev->wheel && item == TRAY_VOLUME && audio_available()) {
        int volume = CLAMP((int)audio_get_volume() + ev->wheel * 5, 0, 100);
        audio_set_volume((uint8_t)volume);
        if (audio_is_muted() && ev->wheel > 0)
            audio_set_muted(false);
        tooltip_hide();
        hover_target = HOVER_TRAY + TRAY_VOLUME;
        hover_since = timer_ticks() - TOOLTIP_DELAY;
        taskbar_dirty = true;
        return;
    }
    if (ev->pressed & MOUSE_RIGHT) {
        open_tray_menu(ev->x, taskbar_rect().y - 1);
        return;
    }
    if (!(ev->pressed & MOUSE_LEFT))
        return;
    switch (item) {
    case TRAY_NETWORK:
        launch_path(PATH_SETTINGS, true);
        break;
    case TRAY_VOLUME:
        if (audio_available())
            audio_set_muted(!audio_is_muted());
        else
            desktop_notify("No sound device found");
        break;
    case TRAY_MEMORY:
        launch_path(PATH_TASKMGR, true);
        break;
    case TRAY_CLOCK:
        open_calendar();
        break;
    default:
        break;
    }
    taskbar_dirty = true;
}

static bool handle_taskbar_click(const mouse_event_t *ev)
{
    for (int i = 0; i < TRAY_COUNT; i++)
        if (rect_contains(tray_item_rect((tray_item_t)i), ev->x, ev->y)) {
            handle_tray_click(ev, (tray_item_t)i);
            return true;
        }
    if (!ev->pressed)
        return true;

    if (rect_contains(start_button_rect(), ev->x, ev->y)) {
        if (ev->pressed & MOUSE_LEFT)
            menu_toggle();
        return true;
    }

    size_t index = 0;
    for (window_t *w = wm_bottom(); w; w = w->above, index++) {
        rect_t r = task_button_rect(index);
        if (!task_button_visible(r) || !rect_contains(r, ev->x, ev->y))
            continue;
        if (ev->pressed & MOUSE_RIGHT) {
            open_window_menu(ev->x, taskbar_rect().y - 1, w);
        } else if (ev->pressed & MOUSE_MIDDLE) {
            wm_close(w);
        } else if (ev->pressed & MOUSE_LEFT) {
            if (w == wm_focused() && !w->minimized)
                wm_toggle_minimize(w);
            else
                wm_focus(w);
        }
        taskbar_dirty = true;
        return true;
    }
    if (ev->pressed & MOUSE_RIGHT)
        open_desktop_menu(ev->x, taskbar_rect().y - 1);
    return true;
}

static void handle_desktop_click(const mouse_event_t *ev)
{
    if (!ev->pressed)
        return;
    int icon = desktop_icon_at(ev->x, ev->y);
    desktop_select(icon);
    if (ev->pressed & MOUSE_RIGHT) {
        if (icon >= 0)
            open_icon_menu(ev->x, ev->y, desktop_icons[icon]);
        else
            open_desktop_menu(ev->x, ev->y);
        return;
    }
    if (!(ev->pressed & MOUSE_LEFT))
        return;
    uint64_t now = timer_ticks();
    if (icon >= 0 && icon == desktop_last_click &&
        now - desktop_last_click_time < DOUBLE_CLICK_MS) {
        desktop_last_click = -1;
        launch_program(desktop_icons[icon]);
        return;
    }
    desktop_last_click = icon;
    desktop_last_click_time = now;
}

/* Returns true when an open popup consumed the event. */
static bool handle_popup_mouse(const mouse_event_t *ev)
{
    if (popup_kind == POPUP_NONE)
        return false;
    if (rect_contains(popup_rect, ev->x, ev->y)) {
        if (popup_kind == POPUP_MENU) {
            int hot = -1;
            for (size_t i = 0; i < popup_count; i++)
                if (popup_items[i].label &&
                    rect_contains(popup_item_rect(i), ev->x, ev->y))
                    hot = (int)i;
            if (hot != popup_hot) {
                popup_hot = hot;
                popup_dirty = true;
            }
            if (ev->released & (MOUSE_LEFT | MOUSE_RIGHT) && hot >= 0)
                popup_activate(hot);
        }
        return true;
    }
    if (ev->pressed) {
        /* A click elsewhere dismisses the popup; a click on the clock
         * must not immediately reopen the calendar it just closed. */
        bool on_clock = popup_kind == POPUP_CALENDAR &&
                        rect_contains(tray_item_rect(TRAY_CLOCK), ev->x, ev->y);
        popup_close();
        return on_clock;
    }
    return false;
}

static void handle_mouse_event(const mouse_event_t *ev)
{
    if (ev->moved)
        track_hover(ev->x, ev->y);
    if (ev->pressed)
        tooltip_hide();

    if (handle_popup_mouse(ev))
        return;

    if (menu_open) {
        rect_t r = menu_rect();
        if (rect_contains(r, ev->x, ev->y)) {
            if (ev->x >= r.x + MENU_WIDTH) {
                for (size_t n = 0;; n++) {
                    size_t app_i = cat_app_at(menu_rows[menu_index].cat, n);
                    if (app_i == SIZE_MAX)
                        break;
                    if (!rect_contains(menu_app_rect(n), ev->x, ev->y))
                        continue;
                    if ((int)n != menu_app_index) {
                        menu_app_index = (int)n;
                        menu_dirty = true;
                    }
                    if (ev->pressed & MOUSE_LEFT) {
                        size_t app_count = 0;
                        const app_entry_t *apps = apps_registry(&app_count);
                        menu_close_and_launch(&apps[app_i]);
                    }
                    return;
                }
                return;
            }
            for (size_t i = 0; i < menu_row_count; i++) {
                if (menu_rows[i].kind == ROW_SEPARATOR)
                    continue;
                if (rect_contains(menu_item_rect(i), ev->x, ev->y)) {
                    if ((int)i != menu_index) {
                        menu_index = (int)i;
                        menu_app_index = -1;
                        menu_dirty = true;
                    }
                    if ((ev->pressed & MOUSE_LEFT) &&
                        menu_rows[i].kind != ROW_CATEGORY)
                        menu_activate(i);
                    return;
                }
            }
            return;
        }
        if (ev->pressed) {
            /* A click outside an open menu just dismisses it; clicking the
             * Start button is the toggle itself. */
            menu_toggle();
            if (rect_contains(start_button_rect(), ev->x, ev->y))
                return;
        }
    }

    if (rect_contains(taskbar_rect(), ev->x, ev->y)) {
        handle_taskbar_click(ev);
        return;
    }

    if (!wm_handle_mouse(ev))
        handle_desktop_click(ev);
    else if (ev->pressed)
        desktop_select(-1);
}

/* Compositing */

static bool rects_overlap(rect_t a, rect_t b)
{
    return !rect_empty(rect_intersect(a, b));
}

static rect_t rect_union_local(rect_t a, rect_t b)
{
    int x0 = MIN(a.x, b.x), y0 = MIN(a.y, b.y);
    int x1 = MAX(a.x + a.w, b.x + b.w);
    int y1 = MAX(a.y + a.h, b.y + b.h);
    return rect_make(x0, y0, x1 - x0, y1 - y0);
}

/* Overlays sit above the windows; anything repainted underneath them has to
 * be covered again. */
static void touch_overlays(rect_t area)
{
    if (rects_overlap(taskbar_rect(), area))
        taskbar_dirty = true;
    if (menu_open && rects_overlap(menu_frame_rect(), area))
        menu_dirty = true;
    if (popup_kind != POPUP_NONE && rects_overlap(popup_rect, area))
        popup_dirty = true;
}

static void draw_overlays(void)
{
    if (taskbar_dirty)
        draw_taskbar();
    if (menu_open && menu_dirty)
        draw_menu();
    if (popup_dirty)
        draw_popup();
    draw_notification();
    draw_tooltip();
}

static void composite(bool full)
{
    if (full) {
        gfx_clip_reset();
        draw_wallpaper();
        for (window_t *w = wm_bottom(); w; w = w->above)
            wm_draw_window(w);
        taskbar_dirty = menu_dirty = true;
        popup_dirty = popup_kind != POPUP_NONE;
        draw_overlays();
        return;
    }

    /* Render invalidated windows independently, then compose every cached
     * surface bottom-to-top.  A lower window can never paint over a higher
     * one just because it happened to be invalidated later. */
    rect_t exposed[16];
    size_t nexposed = wm_take_exposed(exposed, ARRAY_SIZE(exposed));
    rect_t repaint = rect_make(0, 0, 0, 0);
    for (size_t i = 0; i < nexposed; i++) {
        repaint = rect_empty(repaint) ? exposed[i] : rect_union_local(repaint, exposed[i]);
        touch_overlays(exposed[i]);
    }
    for (window_t *w = wm_bottom(); w; w = w->above)
        if (!w->minimized && w->needs_redraw)
            repaint = rect_empty(repaint) ? w->frame : rect_union_local(repaint, w->frame);

    if (!rect_empty(repaint)) {
        gfx_clip_push(repaint);
        draw_wallpaper();
        gfx_clip_pop();
        for (window_t *w = wm_bottom(); w; w = w->above) {
            if (w->minimized || !rects_overlap(w->frame, repaint))
                continue;
            if (w->needs_redraw || !wm_window_cached(w))
                wm_draw_window(w);
            gfx_clip_push(repaint);
            wm_blit_window(w);
            gfx_clip_pop();
            touch_overlays(w->frame);
        }
    }
    draw_overlays();
}

/* Display state shared by successive kernel loop ticks. */
static uint64_t last_frame;
static bool pending_full;
static size_t last_window_count;
static window_t *last_focused;

void desktop_start(void)
{
    wm_init();
    desktop_user_window_init();
    apps_discover();
    build_menu();
    build_desktop_icons();
    mouse_set_bounds(gfx_width(), gfx_height());

    klog("[desk] desktop starting (%dx%d, %u menu categories, %u icons)\n",
         gfx_width(), gfx_height(), (uint32_t)menu_cat_count,
         (uint32_t)desktop_icon_count);

    composite(true);
    int mx, my;
    mouse_get_position(&mx, &my);
    cursor_save_and_draw(mx, my);
    gfx_present();
    cursor_restore();

    last_frame = 0;
    pending_full = false;
}

bool desktop_step(void *context)
{
    UNUSED(context);
    bool dirty = false;
    int mx, my;

    /* Keyboard */
    key_event_t key;
    while (keyboard_poll(&key)) {
        if (handle_global_key(&key))
            dirty = true;
        else if (wm_handle_key(&key))
            dirty = true;
    }

    /* Mouse */
    mouse_event_t mouse;
    bool moved = false;
    while (mouse_poll(&mouse)) {
        handle_mouse_event(&mouse);
        moved = true;
    }

    /* Application ticks */
    for (window_t *w = wm_bottom(); w; w = w->above)
        if (w->cls->on_tick && w->cls->on_tick(w)) {
            wm_invalidate(w);
            dirty = true;
        }

    /* Window list changes (apps opening/closing windows on their own)
     * must reach the taskbar buttons. */
    if (wm_count() != last_window_count || wm_focused() != last_focused) {
        last_window_count = wm_count();
        last_focused = wm_focused();
        taskbar_dirty = true;
    }

    /* Clock and tray status, once a second. */
    uint32_t second = (uint32_t)(timer_ticks() / 1000);
    if (second != last_clock_second) {
        last_clock_second = second;
        taskbar_dirty     = true;
        if (popup_kind == POPUP_CALENDAR)
            popup_dirty = true;
        dirty             = true;
    }

    /* Tooltip after the pointer rests on something for a moment. */
    if (hover_target && !tooltip_visible &&
        timer_ticks() - hover_since > TOOLTIP_DELAY) {
        mouse_get_position(&mx, &my);
        tooltip_show(hover_target, mx, my);
        dirty = true;
    }

    /* Notification expiry */
    if (notification[0] && timer_ticks() > notification_until) {
        notification[0] = '\0';
        wm_expose_rect(notification_rect);
        dirty = true;
    }

    /* A full repaint may be requested in a frame we end up skipping, so
     * the request has to survive until we actually draw. */
    pending_full |= wm_take_full_redraw();
    if (wm_any_dirty() || taskbar_dirty || menu_dirty || popup_dirty)
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

    return false;
}
