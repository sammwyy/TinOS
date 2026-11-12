#include <tinos/string.h>
#include <tinos/widgets.h>

static tinos_widget_view_t ui;
static tinos_system_info_t info;
static int category, page = -1, keyboard_layout;

typedef struct {
    const char *name, *summary, *icon;
} panel_page_t;

static const panel_page_t network[] = {
    { "Status", "Current address and adapter state", "info" },
    { "IP Configuration", "DHCP or a manual address", "chip" },
    { "Connectivity", "DNS resolution and ping", "terminal" },
};
static const panel_page_t display[] = {
    { "Resolution", "Screen mode and text scale", "chip" },
    { "Appearance", "Desktop colours and wallpaper", "tinos" },
};
static const panel_page_t sound[] = {
    { "Output", "Audio device and playback settings", "file" },
};
static const panel_page_t keyboard[] = {
    { "Layout", "Keyboard language and keymap", "editor" },
};
static const struct {
    const char *name, *summary, *icon;
    const panel_page_t *pages;
    unsigned count;
} categories[] = {
    { "Network", "Connections, IP and diagnostics", "chip", network, 3 },
    { "Display", "Resolution and desktop presentation", "info", display, 2 },
    { "Sound", "Audio output and volume", "file", sound, 1 },
    { "Keyboard", "Layout and input behaviour", "editor", keyboard, 1 },
};

static int sidebar_width(void)
{
    return ui.width >= 620 ? 210 : 140;
}

static int category_step(void) { return ui.height < 380 ? 28 : 32; }
static int page_step(void) { return ui.height < 380 ? 20 : 24; }

static int category_y(unsigned index)
{
    int y = ui.height < 380 ? 32 : 40;
    for (unsigned i = 0; i < index; i++)
        y += category_step() + (int)categories[i].count * page_step();
    return y;
}

static void paint(tinos_widget_view_t *view, tinos_canvas_t *canvas,
                  void *context)
{
    (void)context;
    const tinos_gui_theme_t *theme = &view->theme;
    int w = (int)view->width, h = (int)view->height;
    int sidebar = sidebar_width(), content = w - sidebar;
    tinos_canvas_fill(canvas, 0, 0, w, h, theme->background);
    tinos_canvas_fill(canvas, 0, 0, sidebar, h, 0xe1e0e7u);
    tinos_canvas_fill(canvas, sidebar, 0, 1, h, theme->border);
    tinos_canvas_text(canvas, 16, 10, sidebar - 28,
                      "Control Panel", theme->foreground);

    for (unsigned i = 0; i < 4; i++) {
        int y = category_y(i);
        if (y + 28 > h)
            break;
        if ((int)i == category)
            tinos_canvas_fill(canvas, 8, y, sidebar - 16, 28,
                              theme->selection);
        tinos_canvas_icon(canvas, categories[i].icon, 16, y + 5, 18);
        tinos_canvas_text(canvas, 42, y + 6, sidebar - 50,
                          categories[i].name,
                          (int)i == category ? 0xffffffu :
                                               theme->foreground);
        for (unsigned j = 0; j < categories[i].count; j++) {
            int sy = y + category_step() + (int)j * page_step();
            if (sy + page_step() - 2 > h)
                break;
            if ((int)i == category && (int)j == page)
                tinos_canvas_fill(canvas, 24, sy, sidebar - 32,
                                  page_step() - 2,
                                  theme->accent);
            tinos_canvas_icon(canvas, categories[i].pages[j].icon,
                              28, sy + 3, 16);
            tinos_canvas_text(canvas, 49, sy + 3, sidebar - 54,
                              categories[i].pages[j].name,
                              (int)i == category && (int)j == page ?
                              0xffffffu : theme->muted);
        }
    }

    const char *title = page < 0 ? categories[category].name :
                        categories[category].pages[page].name;
    const char *summary = page < 0 ? categories[category].summary :
                          categories[category].pages[page].summary;
    int x = sidebar + 22;
    tinos_canvas_text(canvas, x, 16, content - 44, title, theme->foreground);
    tinos_canvas_text(canvas, x, 36, content - 44, summary, theme->muted);
    tinos_canvas_fill(canvas, sidebar + 18, 54, content - 36, 1,
                      theme->border);
    if (page < 0) {
        unsigned cards = categories[category].count;
        int cols = content >= 500 ? 2 : 1;
        int cw = cols == 2 ? (content - 62) / 2 : content - 48;
        int ch = content >= 500 ? 76 : 65;
        for (unsigned i = 0; i < cards; i++) {
            int cx = sidebar + 24 + (i % cols) * (cw + 14);
            int cy = 76 + (i / cols) * (ch + 12);
            if (cy + ch > h)
                break;
            tinos_canvas_fill(canvas, cx, cy, cw, ch, theme->panel_light);
            tinos_canvas_frame(canvas, cx, cy, cw, ch, theme->border);
            tinos_canvas_icon(canvas, categories[category].pages[i].icon,
                              cx + 14, cy + 16, 32);
            tinos_canvas_text(canvas, cx + 56, cy + 14, cw - 62,
                              categories[category].pages[i].name,
                              theme->foreground);
            tinos_canvas_text(canvas, cx + 56, cy + 34, cw - 62,
                              categories[category].pages[i].summary,
                              theme->muted);
        }
    } else if (category == 3) {
        tinos_canvas_icon(canvas, "editor", x, 78, 32);
        tinos_canvas_text(canvas, x + 44, 83, content - 72,
                          "Keyboard layout", theme->foreground);
        tinos_canvas_text(canvas, x, 130, content - 44,
                          keyboard_layout == TINOS_KBD_ES ?
                          "Spanish (ES)" : "English (US)", theme->foreground);
        tinos_canvas_button(canvas, x, 162, 116, 30, "English", false);
        tinos_canvas_button(canvas, x + 126, 162, 116, 30, "Spanish", false);
        tinos_canvas_text(canvas, x, 211, content - 44,
                          "Press 1 for US or 2 for ES", theme->muted);
    } else if (category == 1 && page == 0) {
        char number[11];
        tinos_u32(number, info.screen_width);
        tinos_canvas_text(canvas, x, 90, content - 44,
                          "Display (read-only)", theme->foreground);
        tinos_canvas_text(canvas, x, 118, content - 44, number,
                          theme->foreground);
        tinos_u32(number, info.screen_height);
        tinos_canvas_text(canvas, x + 52, 118, content - 96, "x",
                          theme->muted);
        tinos_canvas_text(canvas, x + 72, 118, content - 116, number,
                          theme->foreground);
    } else {
        tinos_canvas_text(canvas, x, 92, content - 44,
                          "Status is read-only in userspace.", theme->muted);
    }
}

static void show_settings(void)
{
    tinos_sysinfo(&info);
    keyboard_layout = tinos_ioctl(0, TINOS_IOCTL_KBD_GET_LAYOUT, 0);
    ui.dirty = true;
    tinos_widget_flush(&ui);
}

int main(void)
{
    tinos_system_info_t system;
    bool roomy = tinos_sysinfo(&system) == 0 &&
                 system.memory_total_kib >= 12288;
    if (tinos_widget_open(&ui, "Control Panel",
                          roomy ? 720 : 480, roomy ? 470 : 350) < 0)
        return 1;
    ui.on_paint = paint;
    show_settings();
    tinos_gui_event_t event;
    while (tinos_gui_event(ui.handle, &event) == 1) {
        if (event.type == TINOS_GUI_CLOSE ||
            (event.type == TINOS_GUI_KEY &&
             event.code == TINOS_KEY_ESCAPE))
            break;
        if (event.type == TINOS_GUI_RESIZE)
            show_settings();
        if (event.type == TINOS_GUI_MOUSE && event.code == 1) {
            int sidebar = sidebar_width();
            if (event.x < sidebar) {
                for (unsigned i = 0; i < 4; i++) {
                    int y = category_y(i);
                    if (event.y >= y && event.y < y + 28) {
                        category = (int)i;
                        page = -1;
                        show_settings();
                        break;
                    }
                    if (event.y >= y + category_step() &&
                        event.y < y + category_step() +
                                  (int)categories[i].count * page_step()) {
                        category = (int)i;
                        page = (event.y - y - category_step()) / page_step();
                        show_settings();
                        break;
                    }
                }
            } else if (page < 0 && event.y >= 76) {
                int content = ui.width - sidebar;
                int cols = content >= 500 ? 2 : 1;
                int cw = cols == 2 ? (content - 62) / 2 : content - 48;
                int ch = content >= 500 ? 76 : 65;
                int col = cols == 2 && event.x >= sidebar + 24 + cw + 14;
                int row = (event.y - 76) / (ch + 12);
                int item = row * cols + col;
                if (item >= 0 && item < (int)categories[category].count) {
                    page = item;
                    show_settings();
                }
            } else if (category == 3 && page == 0 &&
                       event.y >= 162 && event.y < 192) {
                int key = event.x >= sidebar + 22 + 126 ?
                          TINOS_KBD_ES : TINOS_KBD_US;
                tinos_ioctl(0, TINOS_IOCTL_KBD_SET_LAYOUT, key);
                show_settings();
            }
        }
        if (event.type == TINOS_GUI_KEY &&
            (event.ch == '1' || event.ch == '2')) {
            tinos_ioctl(0, TINOS_IOCTL_KBD_SET_LAYOUT,
                        event.ch == '1' ? TINOS_KBD_US : TINOS_KBD_ES);
            category = 3;
            page = 0;
            show_settings();
        }
    }
    tinos_widget_close(&ui);
    return 0;
}
