#include <tinos/string.h>
#include <tinos/widgets.h>

extern const char *const tinos_ui_icon_names[];
extern const unsigned tinos_ui_icon_count;
extern const uint32_t tinos_ui_icons[][256];
extern const uint32_t tinos_ui_logo[44 * 44];

void tinos_canvas_icon(tinos_canvas_t *canvas, const char *name,
                       int x, int y, unsigned size)
{
    if (!canvas || !name || size < 1 || size > 64)
        return;
    if (x >= canvas->tx + canvas->width ||
        y >= canvas->ty + canvas->height ||
        x + (int)size <= canvas->tx ||
        y + (int)size <= canvas->ty)
        return;
    uint32_t ink = canvas->view->theme.foreground;
    if (strcmp(name, "play") == 0) {
        for (int row = 0; row < (int)size; row++) {
            int width = row < (int)size / 2 ? row * 2 / 3 + 2 :
                         ((int)size - row) * 2 / 3 + 2;
            tinos_canvas_fill(canvas, x + 3, y + row, width, 1, ink);
        }
        return;
    }
    if (strcmp(name, "pause") == 0) {
        tinos_canvas_fill(canvas, x + 3, y + 2, 4, size - 4, ink);
        tinos_canvas_fill(canvas, x + (int)size - 7, y + 2,
                          4, size - 4, ink);
        return;
    }
    if (strcmp(name, "volume") == 0) {
        tinos_canvas_fill(canvas, x + 2, y + 6, 4, 5, ink);
        tinos_canvas_fill(canvas, x + 6, y + 4, 3, 9, ink);
        tinos_canvas_fill(canvas, x + 10, y + 3, 2, 1, ink);
        tinos_canvas_fill(canvas, x + 10, y + (int)size - 5, 2, 1, ink);
        tinos_canvas_fill(canvas, x + 12, y + 4, 1, size - 8, ink);
        return;
    }
    for (unsigned i = 0; i < tinos_ui_icon_count; i++) {
        if (strcmp(name, tinos_ui_icon_names[i]) != 0)
            continue;
        bool large = strcmp(name, "tinos") == 0 && size > 24;
        unsigned top = (unsigned)(y < canvas->ty ? canvas->ty - y : 0);
        unsigned left = (unsigned)(x < canvas->tx ? canvas->tx - x : 0);
        unsigned bottom = (unsigned)(y + (int)size > canvas->ty + canvas->height ?
                                     canvas->ty + canvas->height - y : (int)size);
        unsigned right = (unsigned)(x + (int)size > canvas->tx + canvas->width ?
                                    canvas->tx + canvas->width - x : (int)size);
        for (unsigned py = top; py < bottom; py++)
            for (unsigned px = left; px < right; px++) {
                uint32_t color = large ?
                    tinos_ui_logo[(py * 44 / size) * 44 + px * 44 / size] :
                    tinos_ui_icons[i][(py * 16 / size) * 16 +
                                      px * 16 / size];
                if (color >> 24)
                    tinos_canvas_fill(canvas, x + (int)px, y + (int)py,
                                      1, 1, color & 0x00ffffffu);
            }
        return;
    }
}
