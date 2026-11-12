#include <tinos/widgets.h>

static tinos_widget_view_t ui;
static const char *const about_lines[] = {
    "TinOS 1.0.0",
    "A tiny graphical operating system for your computer.",
    "",
    "It includes a desktop with a start menu, a command",
    "line, a file manager, a text editor and more.",
    "",
    "Everything here runs from memory, so the system feels",
    "fast and responsive, just like a real desktop.",
    "",
    "Shortcuts: Win/Ctrl+Esc start menu, Alt+Tab switch windows,",
    "Alt+F4 close the current window.",
};

static void paint(tinos_widget_view_t *view, tinos_canvas_t *canvas,
                  void *context)
{
    (void)context;
    const tinos_gui_theme_t *theme = &view->theme;
    tinos_canvas_fill(canvas, 0, 0, view->width, view->height,
                      theme->background);
    tinos_canvas_gradient(canvas, 0, 0, view->width, 74,
                          theme->accent, theme->background);
    tinos_canvas_icon(canvas, "tinos", 18, 14, 44);
    tinos_canvas_text(canvas, 76, 22, view->width - 88,
                      "TinOS 1.0.0", theme->foreground);
    tinos_canvas_text(canvas, 76, 42, view->width - 88,
                      "Lightweight and built to run on anything", theme->muted);
    for (unsigned i = 0; i < sizeof(about_lines) / sizeof(about_lines[0]); i++)
        tinos_canvas_text(canvas, 18, 88 + (int)i * 18,
                          view->width - 36, about_lines[i],
                          theme->foreground);
}

int main(void)
{
    tinos_system_info_t info;
    bool roomy = tinos_sysinfo(&info) == 0 &&
                 info.memory_total_kib >= 12288;
    if (tinos_widget_open(&ui, "About TinOS",
                          roomy ? 512 : 440, roomy ? 420 : 320) < 0)
        return 1;
    ui.on_paint = paint;
    if (tinos_widget_flush(&ui) < 0) {
        tinos_widget_close(&ui);
        return 1;
    }

    tinos_gui_event_t event;
    while (tinos_gui_event(ui.handle, &event) == 1) {
        if (event.type == TINOS_GUI_CLOSE ||
            (event.type == TINOS_GUI_KEY && event.code == TINOS_KEY_ESCAPE))
            break;
        if (event.type == TINOS_GUI_RESIZE)
            ui.dirty = true;
        if (tinos_widget_flush(&ui) < 0)
            break;
    }
    tinos_widget_close(&ui);
    return 0;
}
