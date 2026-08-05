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
#include <tinos/async.h>
#include <tinos/desktop.h>
#include <tinos/fs.h>
#include <tinos/gfx.h>
#include <tinos/heap.h>
#include <tinos/input.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/net.h>
#include <tinos/printf.h>
#include <tinos/sh.h>
#include <tinos/string.h>
#include <tinos/ui.h>
#include <tinos/thread.h>
#include <tinos/timer.h>

#define MENU_WIDTH      160
#define MENU_SUBMENU_W  220
#define MENU_ITEM_H     24
#define MENU_CAT_H      24
#define MENU_HEADER_H   38
#define MENU_MAX_CATS   8
#define MENU_MAX_ROWS   24
#define START_BUTTON_W  84
#define TASK_BUTTON_W   148
#define CLOCK_WIDTH     88
#define WIDGET_AREA_W   (CLOCK_WIDTH + 124) /* clock + memory gauge + label */
#define NOTIFY_MS       2600

typedef enum {
    ROW_APP = 0,
    ROW_CATEGORY,
    ROW_SEPARATOR,
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

/* Callers set the clip: the full workspace for a full repaint, or a single
 * exposed rect when repairing the area a moved/closed/minimised window
 * uncovered. */
static void draw_wallpaper(void)
{
    rect_t space = wm_workspace();

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
    };
    for (size_t i = 0; i < ARRAY_SIZE(lines); i++)
        gfx_text(cx - gfx_text_width(lines[i]) / 2, cy + 28 + (int)i * 20,
                 lines[i], RGB(0x9e, 0xaa, 0xc0));
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
    widget_icon(start.x + 5, start.y + 4, 18, ICON_TINOS);
    gfx_text(start.x + 30, start.y + (start.h - FONT_H) / 2, "Start",
             menu_open ? RGB(0xf0, 0xf2, 0xf5) : COLOR_TEXT);

    /* One button per window, highlighting the focused one. */
    window_t *focused = wm_focused();
    size_t    index   = 0;
    for (window_t *w = wm_bottom(); w; w = w->above, index++) {
        rect_t r = task_button_rect(index);
        if (r.x + r.w > bar.w - WIDGET_AREA_W)
            break;
        bool active = (w == focused) && !w->minimized;
        gfx_fill_rect(r.x, r.y, r.w, r.h,
                      active ? COLOR_PANEL_LIGHT : COLOR_TASKBAR);
        gfx_bevel(r.x, r.y, r.w, r.h, active ? COLOR_PANEL_LIGHT : COLOR_PANEL,
                  active ? COLOR_BORDER : COLOR_TASKBAR_EDGE);
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
    gfx_vline(right - 112, bar.y + 5, bar.h - 10, COLOR_BORDER);

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
    gfx_fill_rect(gauge_x, gauge_y, gauge_w, 10, RGB(0x45, 0x4e, 0x5d));
    gfx_fill_rect(gauge_x, gauge_y, CLAMP(used, 1, gauge_w), 10, COLOR_ACCENT);
    gfx_rect(gauge_x, gauge_y, gauge_w, 10, COLOR_BORDER);

    char clock[32];
    format_clock(clock, sizeof(clock));
    gfx_fill_rect(right, bar.y + 3, CLOCK_WIDTH, bar.h - 6, COLOR_TASKBAR);
    gfx_bevel(right, bar.y + 3, CLOCK_WIDTH, bar.h - 6, COLOR_BORDER,
              COLOR_PANEL_LIGHT);
    widget_text_centered(rect_make(right, bar.y + 4, CLOCK_WIDTH, bar.h - 8),
                         clock, COLOR_TEXT);

    gfx_clip_pop();
    taskbar_dirty = false;
}

/* ------------------------------------------------------------------ */
/* Start menu                                                          */
/* ------------------------------------------------------------------ */

static void menu_rebuild_rows(void);

static icon_t menu_category_icon(const char *name)
{
    if (strcmp(name, "Accessories") == 0)
        return ICON_FOLDER;
    if (strcmp(name, "System") == 0)
        return ICON_CHIP;
    return ICON_INFO;
}

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
            (menu_category_t){ name, menu_category_icon(name) };
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

/* Rebuild the flat list of visible rows from the categories and their
 * then the power actions at the bottom. Applications are drawn in the
 * right-hand submenu instead of being inserted into this list. */
static void menu_rebuild_rows(void)
{
    menu_row_count = 0;
    for (size_t c = 0; c < menu_cat_count; c++) {
        menu_rows[menu_row_count++] = (menu_row_t){ ROW_CATEGORY, c, NULL };
    }
    menu_rows[menu_row_count++] = (menu_row_t){ ROW_SEPARATOR, 0, NULL };
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
    if (menu_cat_count) {
        for (size_t n = 0; cat_app_at(0, n) != SIZE_MAX; n++)
            app_count++;
    }
    for (size_t c = 1; c < menu_cat_count; c++) {
        int count = 0;
        for (size_t n = 0; cat_app_at(c, n) != SIZE_MAX; n++)
            count++;
        if (count > app_count)
            app_count = count;
    }
    int submenu_height = app_count * MENU_ITEM_H + 12;
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
        widget_icon(item.x + 8, item.y + (item.h - 18) / 2, 18,
                    menu_cats[row->cat].icon);
        gfx_char(item.x + item.w - 16, item.y + (item.h - FONT_H) / 2,
                 '\x10', selected ? COLOR_TEXT : COLOR_ACCENT);
        gfx_text(item.x + 34, item.y + (item.h - FONT_H) / 2,
                 menu_cats[row->cat].name,
                 selected ? RGB(0xff, 0xff, 0xff) : COLOR_TEXT);
        return;
    }

    const char *label;
    icon_t      icon;
    bool        indented = row->kind == ROW_APP;
    if (row->kind == ROW_APP) {
        label = row->app->name;
        icon  = row->app->icon;
    } else if (row->kind == ROW_RESTART) {
        label = "Restart";
        icon  = ICON_RESTART;
    } else {
        label = "Shut Down";
        icon  = ICON_POWER;
    }

    if (selected) {
        gfx_fill_rect(item.x, item.y, item.w, item.h, COLOR_ACCENT_DARK);
        gfx_rect(item.x, item.y, item.w, item.h, COLOR_ACCENT);
    }
    int ix = item.x + (indented ? 24 : 6);
    int tx = item.x + (indented ? 50 : 32);
    widget_icon(ix, item.y + (item.h - 18) / 2, 18, icon);
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
        widget_icon(item.x + 8, item.y + (item.h - 18) / 2, 18, app->icon);
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
    widget_icon(r.x + 10, r.y + 8, 26, ICON_TINOS);
    gfx_text(r.x + 46, r.y + 10, TINOS_NAME " " TINOS_VERSION,
             RGB(0xf2, 0xf4, 0xf7));
    gfx_text(r.x + 46, r.y + 26, "applications", RGB(0xd0, 0xda, 0xe5));
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

static void menu_toggle(void)
{
    menu_open = !menu_open;
    if (menu_open) {
        menu_index = 0;
        menu_app_index = -1;
        menu_dirty = true;
    }
    /* Opening or closing the menu uncovers part of the desktop. */
    wm_invalidate_all();
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

static void menu_activate(size_t index)
{
    if (index >= menu_row_count)
        return;
    menu_row_t *row = &menu_rows[index];

    switch (row->kind) {
    case ROW_APP:
        menu_open = false;
        wm_invalidate_all();
        taskbar_dirty = true;
        if (row->app && row->app->launch)
            row->app->launch(NULL);
        break;
    case ROW_CATEGORY:
        menu_app_index = 0;
        menu_dirty = true;
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
            if (menu_app_index >= 0 &&
                menu_rows[menu_index].kind == ROW_CATEGORY) {
                size_t app_count = 0;
                const app_entry_t *apps = apps_registry(&app_count);
                size_t app_i = cat_app_at(menu_rows[menu_index].cat,
                                          (size_t)menu_app_index);
                if (app_i != SIZE_MAX && apps[app_i].launch) {
                    menu_open = false;
                    wm_invalidate_all();
                    taskbar_dirty = true;
                    apps[app_i].launch(NULL);
                }
            } else {
                menu_activate((size_t)menu_index);
            }
            return true;
        case KEY_RIGHT:
            if (menu_index >= 0 &&
                menu_rows[menu_index].kind == ROW_CATEGORY)
                menu_app_index = 0;
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
            if (ev->x >= r.x + MENU_WIDTH) {
                for (size_t n = 0;; n++) {
                    size_t app_i = cat_app_at(menu_rows[menu_index].cat, n);
                    if (app_i == SIZE_MAX)
                        break;
                    if (!rect_contains(menu_app_rect(n), ev->x, ev->y))
                        continue;
                    menu_app_index = (int)n;
                    menu_dirty = true;
                    if (ev->pressed & MOUSE_LEFT) {
                        size_t app_count = 0;
                        const app_entry_t *apps = apps_registry(&app_count);
                        menu_open = false;
                        wm_invalidate_all();
                        taskbar_dirty = true;
                        if (apps[app_i].launch)
                            apps[app_i].launch(NULL);
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
        gfx_clip_reset();
        draw_wallpaper();
        for (window_t *w = wm_bottom(); w; w = w->above)
            wm_draw_window(w);
        draw_taskbar();
        draw_menu();
        draw_notification();
        return;
    }

    /* Repair only the rectangles a moved/closed/minimised window uncovered,
     * instead of repainting the whole desktop. */
    rect_t exposed[16];
    size_t nexposed = wm_take_exposed(exposed, ARRAY_SIZE(exposed));
    for (size_t i = 0; i < nexposed; i++) {
        gfx_clip_push(exposed[i]);
        draw_wallpaper();
        gfx_clip_pop();
        if (rects_overlap(taskbar_rect(), exposed[i]))
            taskbar_dirty = true;
        if (menu_open && rects_overlap(menu_frame_rect(), exposed[i]))
            menu_dirty = true;
    }

    /* A window can overlap an exposed region without being the window that
     * caused it (for example, when the top window is moved or closed).  Draw
     * every intersecting window in z-order, otherwise the repaired wallpaper
     * can remain visible on top of a window. */
    for (window_t *w = wm_bottom(); w; w = w->above) {
        if (w->minimized)
            continue;

        bool redraw = w->needs_redraw;
        for (size_t i = 0; i < nexposed; i++)
            if (rects_overlap(w->frame, exposed[i])) {
                redraw = true;
                break;
            }
        bool overlaps_exposed = false;
        for (size_t i = 0; i < nexposed; i++)
            if (rects_overlap(w->frame, exposed[i])) {
                overlaps_exposed = true;
                break;
            }
        if (!redraw && !overlaps_exposed)
            continue;

        if (redraw || !wm_window_cached(w))
            wm_draw_window(w);
        else
            wm_blit_window(w);
        if (rects_overlap(taskbar_rect(), w->frame))
            taskbar_dirty = true;
        if (menu_open && rects_overlap(menu_frame_rect(), w->frame))
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

    klog("[desk] desktop starting (%dx%d, %u menu categories)\n", gfx_width(),
         gfx_height(), (uint32_t)menu_cat_count);

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

        /* Future network/storage workers execute outside interrupt context.
         * The budget prevents one busy subsystem from starving the GUI. */
        async_run(8);
        thread_run(4);
        ktimer_poll(4);

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
