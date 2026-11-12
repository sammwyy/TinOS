#include <tinos/string.h>
#include <tinos/ui.h>

static tinos_ui_t ui;
static uint32_t pixels[TINOS_GUI_IMAGE_WIDTH * TINOS_GUI_IMAGE_HEIGHT];
static uint8_t row[4 * TINOS_GUI_IMAGE_WIDTH];

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int read_exact(int fd, void *output, unsigned length)
{
    uint8_t *bytes = output;
    while (length) {
        unsigned count = length > 1024 ? 1024 : length;
        int got = tinos_read(fd, bytes, count);
        if (got <= 0)
            return -1;
        bytes += got;
        length -= got;
    }
    return 0;
}

static int load_bmp(const char *path, unsigned *width, unsigned *height)
{
    int fd = tinos_open(path);
    if (fd < 0)
        return -1;
    uint8_t header[54];
    if (read_exact(fd, header, sizeof(header)) < 0) {
        tinos_close(fd);
        return -1;
    }
    int good = header[0] == 'B' && header[1] == 'M' &&
               read_u32(header + 14) >= 40 && read_u16(header + 26) == 1 &&
               read_u32(header + 30) == 0;
    unsigned w = read_u32(header + 18);
    unsigned raw_h = read_u32(header + 22);
    unsigned h = (raw_h & 0x80000000u) ? 0u - raw_h : raw_h;
    unsigned bpp = read_u16(header + 28);
    unsigned offset = read_u32(header + 10);
    if (!good || !w || w > TINOS_GUI_IMAGE_WIDTH || !h ||
        h > TINOS_GUI_IMAGE_HEIGHT || (bpp != 24 && bpp != 32) ||
        offset < sizeof(header) || offset > 4096) {
        tinos_close(fd);
        return -1;
    }
    unsigned skip = offset - sizeof(header);
    while (skip) {
        unsigned count = skip > sizeof(row) ? sizeof(row) : skip;
        if (read_exact(fd, row, count) < 0) {
            tinos_close(fd);
            return -1;
        }
        skip -= count;
    }
    unsigned stride = (w * (bpp / 8) + 3) & ~3u;
    for (unsigned y = 0; y < h; y++) {
        if (read_exact(fd, row, stride) < 0) {
            tinos_close(fd);
            return -1;
        }
        unsigned dst = (raw_h & 0x80000000u) ? y : h - 1 - y;
        for (unsigned x = 0; x < w; x++) {
            const uint8_t *pixel = row + x * (bpp / 8);
            pixels[dst * w + x] = 0xff000000u | ((uint32_t)pixel[2] << 16) |
                                  ((uint32_t)pixel[1] << 8) | pixel[0];
        }
    }
    tinos_close(fd);
    *width = w;
    *height = h;
    return 0;
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
