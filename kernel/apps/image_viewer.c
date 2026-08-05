#include <tinos/fs.h>
#include <tinos/heap.h>
#include <tinos/image.h>
#include <tinos/printf.h>
#include <tinos/string.h>
#include <tinos/ui.h>

typedef struct {
    image_t image;
    char error[64];
    bool loaded;
} image_viewer_t;

static void image_extension(const char *path, char *out, size_t size)
{
    const char *name = strrchr(path, '/');
    const char *dot = strrchr(name ? name : path, '.');
    if (!dot || dot[1] == '\0') {
        out[0] = '\0';
        return;
    }
    strlcpy(out, dot + 1, size);
}

static void image_viewer_on_open(window_t *win, const char *arg)
{
    image_viewer_t *viewer = kcalloc(1, sizeof(*viewer));
    if (!viewer) {
        strlcpy(win->title, "Image Viewer - out of memory", sizeof(win->title));
        return;
    }
    win->priv = viewer;

    fs_node_t *node = NULL;
    if (!arg) {
        strlcpy(viewer->error, "Open an image from File Explorer",
                sizeof(viewer->error));
        return;
    }
    if (fs_resolve(fs_root(), arg, &node) != FS_OK || node->type != FS_FILE) {
        strlcpy(viewer->error, "File not found", sizeof(viewer->error));
        return;
    }
    char extension[16];
    image_extension(arg, extension, sizeof(extension));
    image_result_t result = image_decode(fs_data(node), fs_size(node),
                                         extension, &viewer->image);
    if (result != IMAGE_OK)
        strlcpy(viewer->error, image_result_string(result),
                sizeof(viewer->error));
    else
        viewer->loaded = true;
    wm_set_title(win, node->name);
}

static void image_viewer_on_close(window_t *win)
{
    image_viewer_t *viewer = win->priv;
    if (viewer) {
        image_destroy(&viewer->image);
        kfree(viewer);
    }
    win->priv = NULL;
}

static void image_viewer_on_draw(window_t *win, rect_t client)
{
    image_viewer_t *viewer = win->priv;
    gfx_fill_rect(client.x, client.y, client.w, client.h, RGB(0x24, 0x28, 0x31));
    if (!viewer || !viewer->loaded) {
        if (viewer)
            widget_text_centered(client, viewer->error, COLOR_DANGER);
        return;
    }

    int max_w = client.w - 20;
    int max_h = client.h - 42;
    int draw_w = max_w;
    int draw_h = (int)((uint64_t)max_w * viewer->image.height /
                       viewer->image.width);
    if (draw_h > max_h) {
        draw_h = max_h;
        draw_w = (int)((uint64_t)max_h * viewer->image.width /
                       viewer->image.height);
    }
    rect_t image = rect_make(client.x + (client.w - draw_w) / 2,
                             client.y + (max_h - draw_h) / 2,
                             draw_w, draw_h);
    for (int y = image.y; y < image.y + image.h; y += 16)
        for (int x = image.x; x < image.x + image.w; x += 16)
            gfx_fill_rect(x, y, MIN(16, image.x + image.w - x),
                          MIN(16, image.y + image.h - y),
                          ((x / 16 + y / 16) & 1) ? RGB(0x9a, 0x9a, 0xa2)
                                                  : RGB(0xe1, 0xe1, 0xe5));
    gfx_blit_image(image, viewer->image.pixels, (int)viewer->image.width,
                   (int)viewer->image.height);

    char info[64];
    ksnprintf(info, sizeof(info), "%u x %u", viewer->image.width,
              viewer->image.height);
    gfx_text(client.x + 8, client.y + client.h - FONT_H - 6, info,
             RGB(0xe0, 0xe3, 0xeb));
}

static bool image_viewer_on_key(window_t *win, const key_event_t *event)
{
    if (event->code == KEY_ESCAPE) {
        wm_close(win);
        return true;
    }
    return false;
}

static const app_class_t image_viewer_class = {
    .name = "image-viewer",
    .on_open = image_viewer_on_open,
    .on_close = image_viewer_on_close,
    .on_draw = image_viewer_on_draw,
    .on_key = image_viewer_on_key,
};

window_t *app_image_viewer_open(const char *path)
{
    return wm_open(&image_viewer_class, 700, 500, "Image Viewer", path);
}
