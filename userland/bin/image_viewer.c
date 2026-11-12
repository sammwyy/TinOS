#include <tinos/bmp.h>
#include <tinos/string.h>
#include <tinos/ui.h>

static tinos_ui_t ui;
static uint32_t pixels[TINOS_GUI_IMAGE_WIDTH * TINOS_GUI_IMAGE_HEIGHT];

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

int main(int argc, char **argv)
{
    const char *path = argc > 1 && argv[1][0] == '/' ?
                       argv[1] : "/apps/media/demo.bmp";
    if (tinos_ui_open(&ui, "Image Viewer", 380, 300) < 0)
        return 1;
    if (tinos_gui_blit(ui.handle, (const uint32_t *)0x00100000u, 2, 2) != -1 ||
        tinos_gui_blit(ui.handle, pixels, TINOS_GUI_IMAGE_WIDTH + 1, 1) != -1)
        return 2;
    tinos_ui_text(&ui, 1, 0, path);
    unsigned width, height;
    if (load_bmp(path, &width, &height) == 0 &&
        tinos_gui_blit(ui.handle, pixels, width, height) == 0) {
        tinos_ui_text(&ui, 1, ui.rows - 1, "BMP 24/32-bit  |  Esc closes");
    } else {
        tinos_ui_text(&ui, 1, 3, "Cannot display this image.");
        tinos_ui_text(&ui, 1, 5, "Use an uncompressed 24/32-bit BMP");
        tinos_ui_text(&ui, 1, 6, "up to 320 x 192 pixels.");
    }
    tinos_ui_present(&ui);
    tinos_gui_event_t event;
    while (tinos_gui_event(ui.handle, &event) == 1)
        if (event.type == TINOS_GUI_CLOSE ||
            (event.type == TINOS_GUI_KEY && event.code == TINOS_KEY_ESCAPE))
            break;
    tinos_gui_close(ui.handle);
    return 0;
}
