#include <tinos/string.h>
#include <tinos/widgets.h>

static tinos_widget_view_t ui;
static tinos_system_info_t info;

static void row(tinos_canvas_t *canvas, int y, const char *label,
                const char *value, int width, const tinos_gui_theme_t *theme)
{
    tinos_canvas_text(canvas, 18, y, 126, label, theme->muted);
    tinos_canvas_text(canvas, 148, y, width - 164, value, theme->foreground);
}

static void numeric_row(tinos_canvas_t *canvas, int y, const char *label,
                        uint32_t value, const char *unit, int width,
                        const tinos_gui_theme_t *theme)
{
    char text[32];
    tinos_u32(text, value);
    tinos_strlcpy(text + strlen(text), unit, sizeof(text) - strlen(text));
    row(canvas, y, label, text, width, theme);
}

static void paint(tinos_widget_view_t *view, tinos_canvas_t *canvas,
                  void *context)
{
    (void)context;
    const tinos_gui_theme_t *theme = &view->theme;
    int w = (int)view->width, h = (int)view->height;
    tinos_canvas_fill(canvas, 0, 0, w, h, theme->background);
    tinos_canvas_icon(canvas, "chip", 16, 14, 32);
    tinos_canvas_text(canvas, 60, 22, w - 70,
                      "System Information", theme->foreground);
    tinos_canvas_fill(canvas, 12, 56, w - 24, 1, theme->border);
    int y = 70;
    row(canvas, y, "system", "TinOS 1.0.0", w, theme); y += 22;
    row(canvas, y, "CPU", info.cpu, w, theme); y += 22;
    row(canvas, y, "platform", info.platform, w, theme); y += 22;
    row(canvas, y, "clock", info.clock, w, theme); y += 22;
    numeric_row(canvas, y, "uptime", info.uptime_ms / 1000, " s", w, theme);
    y += 28;
    tinos_canvas_fill(canvas, 12, y, w - 24, 1, theme->border);
    y += 12;
    numeric_row(canvas, y, "RAM total", info.memory_total_kib,
                " KiB", w, theme); y += 22;
    numeric_row(canvas, y, "RAM free", info.memory_free_kib,
                " KiB", w, theme); y += 22;
    char resolution[32];
    tinos_u32(resolution, info.screen_width);
    tinos_strlcpy(resolution + strlen(resolution), " x ",
                  sizeof(resolution) - strlen(resolution));
    char number[11];
    tinos_u32(number, info.screen_height);
    tinos_strlcpy(resolution + strlen(resolution), number,
                  sizeof(resolution) - strlen(resolution));
    row(canvas, y, "display", resolution, w, theme); y += 22;
    numeric_row(canvas, y, "filesystem", info.fs_nodes, " nodes", w, theme);

    int bar_y = h - 35;
    int bar_w = w - 36;
    tinos_canvas_fill(canvas, 18, bar_y, bar_w, 14, 0x181d28u);
    uint32_t used = info.memory_total_kib > info.memory_free_kib ?
                    info.memory_total_kib - info.memory_free_kib : 0;
    int filled = info.memory_total_kib ?
        (int)(used * (uint32_t)bar_w / info.memory_total_kib) : 0;
    tinos_canvas_fill(canvas, 18, bar_y, filled, 14, theme->accent_light);
    tinos_canvas_frame(canvas, 18, bar_y, bar_w, 14, theme->border);
}

static void show_info(void)
{
    if (tinos_sysinfo(&info) == 0) {
        ui.dirty = true;
        tinos_widget_flush(&ui);
    }
}

int main(void)
{
    tinos_system_info_t system;
    bool roomy = tinos_sysinfo(&system) == 0 &&
                 system.memory_total_kib >= 12288;
    if (tinos_widget_open(&ui, "System Information",
                          roomy ? 540 : 480, roomy ? 420 : 355) < 0)
        return 1;
    ui.on_paint = paint;
    tinos_gui_timer(ui.handle, 1000);
    show_info();
    tinos_gui_event_t event;
    while (tinos_gui_event(ui.handle, &event) == 1) {
        if (event.type == TINOS_GUI_CLOSE ||
            (event.type == TINOS_GUI_KEY &&
             event.code == TINOS_KEY_ESCAPE))
            break;
        if (event.type == TINOS_GUI_TIMER ||
            event.type == TINOS_GUI_RESIZE ||
            (event.type == TINOS_GUI_KEY &&
             (event.ch == 'r' || event.ch == 'R')))
            show_info();
    }
    tinos_widget_close(&ui);
    return 0;
}
