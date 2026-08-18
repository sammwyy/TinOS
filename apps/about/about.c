#include <tinos/gfx.h>
#include <tinos/kernel.h>
#include <tinos/ui.h>

static const char *const about_lines[] = {
    "TinOS " TINOS_VERSION,
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

static void about_on_draw(window_t *win, rect_t client)
{
    gfx_fill_rect(client.x, client.y, client.w, client.h, COLOR_WINDOW_BG);
    gfx_gradient_v(client.x, client.y, client.w, 74, COLOR_ACCENT_DARK,
                   COLOR_WINDOW_BG);
    widget_icon(client.x + 18, client.y + 14, 44, ICON_TINOS);
    gfx_text(client.x + 76, client.y + 22, TINOS_NAME " " TINOS_VERSION,
             COLOR_TEXT);
    gfx_text(client.x + 76, client.y + 42, "Lightweight and built to run on anything",
             COLOR_TEXT_DIM);

    int y = client.y + 88;
    for (size_t i = 0; i < ARRAY_SIZE(about_lines); i++, y += FONT_H + 2)
        gfx_text(client.x + 18, y, about_lines[i],
                 about_lines[i][0] == ' ' ? COLOR_TEXT_DIM : COLOR_TEXT);
}

static const app_class_t about_class = {
    .name    = "about",
    .on_draw = about_on_draw,
};

window_t *app_about_open(const char *arg)
{
    return wm_open(&about_class, 512, 420, "About TinOS", arg);
}
