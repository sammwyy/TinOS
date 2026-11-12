#include <tinos/bmp.h>
#include <tinos/string.h>
#include <tinos/widgets.h>

static tinos_widget_view_t ui;
static uint32_t pixels[TINOS_GUI_IMAGE_WIDTH * TINOS_GUI_IMAGE_HEIGHT];
static unsigned image_width, image_height;
static const char *error;

static int read_file(void *context, void *buffer, unsigned length)
{
    return tinos_read(*(int *)context, buffer, length);
}

static int load_bmp(const char *path, unsigned *width, unsigned *height)
{
    int fd = tinos_open(path);
    if (fd < 0)
        return -1;
    int result = tinos_bmp_decode(read_file, &fd, pixels,
                                  TINOS_GUI_IMAGE_WIDTH * TINOS_GUI_IMAGE_HEIGHT,
                                  width, height);
    tinos_close(fd);
    return result;
}

static void paint(tinos_widget_view_t *view, tinos_canvas_t *canvas,
                  void *context)
{
    (void)context;
    int w = (int)view->width, h = (int)view->height;
    tinos_canvas_fill(canvas, 0, 0, w, h, 0x202530u);
    if (!image_width || !image_height) {
        tinos_canvas_text(canvas, 16, h / 2, w - 32,
                          error ? error : "Loading...", view->theme.danger);
        return;
    }
    int max_w = w - 32, max_h = h - 22 - 20;
    int draw_w = max_w;
    int draw_h = (int)(image_height * (unsigned)max_w / image_width);
    if (draw_h > max_h) {
        draw_h = max_h;
        draw_w = (int)(image_width * (unsigned)max_h / image_height);
    }
    int x = 16 + (max_w - draw_w) / 2;
    int y = 8 + (max_h - draw_h) / 2;
    for (int py = y; py < y + draw_h; py += 16)
        for (int px = x; px < x + draw_w; px += 16)
            tinos_canvas_fill(canvas, px, py,
                              (x + draw_w - px < 16 ? x + draw_w - px : 16),
                              (y + draw_h - py < 16 ? y + draw_h - py : 16),
                              ((px / 16 + py / 16) & 1) ?
                              0x9a9aa2u : 0xe1e1e5u);
    tinos_canvas_frame(canvas, x - 1, y - 1, draw_w + 2, draw_h + 2,
                       0x4c5b72u);
    tinos_canvas_image(canvas, x, y, draw_w, draw_h, pixels,
                       image_width, image_height);
    char dimensions[40], number[11];
    tinos_u32(dimensions, image_width);
    tinos_strlcpy(dimensions + strlen(dimensions), " x ",
                  sizeof(dimensions) - strlen(dimensions));
    tinos_u32(number, image_height);
    tinos_strlcpy(dimensions + strlen(dimensions), number,
                  sizeof(dimensions) - strlen(dimensions));
    tinos_canvas_text(canvas, 12, h - 23, 74,
                      dimensions, 0xe0e3ebu);
    tinos_canvas_text(canvas, 90, h - 23, w - 212,
                      "BMP 24/32-bit", 0xe0e3ebu);
    tinos_canvas_text(canvas, w - 112, h - 23, 108,
                      "Fit to window", view->theme.muted);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 && argv[1][0] == '/' ?
                       argv[1] : "/apps/media/demo.bmp";
    tinos_system_info_t system;
    bool roomy = tinos_sysinfo(&system) == 0 &&
                 system.memory_total_kib >= 12288;
    if (tinos_widget_open(&ui, "Image Viewer",
                          roomy ? 640 : 380, roomy ? 430 : 300) < 0)
        return 1;
    if (tinos_gui_blit(ui.handle, (const uint32_t *)0x00100000u, 2, 2) != -1 ||
        tinos_gui_blit(ui.handle, pixels, TINOS_GUI_IMAGE_WIDTH + 1, 1) != -1)
        return 2;
    if (load_bmp(path, &image_width, &image_height) < 0)
        error = "Cannot display this image.";
    ui.on_paint = paint;
    if (tinos_widget_flush(&ui) < 0) {
        tinos_widget_close(&ui);
        return 1;
    }
    tinos_gui_event_t event;
    while (tinos_gui_event(ui.handle, &event) == 1)
        if (event.type == TINOS_GUI_CLOSE ||
            (event.type == TINOS_GUI_KEY && event.code == TINOS_KEY_ESCAPE))
            break;
        else if (event.type == TINOS_GUI_RESIZE) {
            ui.dirty = true;
            tinos_widget_flush(&ui);
        }
    tinos_widget_close(&ui);
    return 0;
}
