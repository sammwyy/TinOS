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
    fs_io_request_t *request;
    char extension[16];
    window_t *window;
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

static void image_viewer_loaded(fs_io_request_t *request, void *arg);

static void image_viewer_on_open(window_t *win, const char *arg)
{
    image_viewer_t *viewer = kcalloc(1, sizeof(*viewer));
    if (!viewer) {
        strlcpy(win->title, "Image Viewer - out of memory", sizeof(win->title));
        return;
    }
    win->priv = viewer;
    viewer->window = win;

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
    image_extension(arg, viewer->extension, sizeof(viewer->extension));
    viewer->request = fs_read_all_async(node, image_viewer_loaded, viewer);
    if (!viewer->request)
        strlcpy(viewer->error, "Unable to start file read", sizeof(viewer->error));
    wm_set_title(win, node->name);
}

static void image_viewer_loaded(fs_io_request_t *request, void *arg)
{
    image_viewer_t *viewer = arg;
    if (!fs_io_ok(request)) {
        strlcpy(viewer->error, "Unable to read file", sizeof(viewer->error));
    } else {
        image_result_t result = image_decode(fs_io_data(request),
                                             fs_io_size(request),
                                             viewer->extension, &viewer->image);
        if (result != IMAGE_OK)
            strlcpy(viewer->error, image_result_string(result),
                    sizeof(viewer->error));
        else
            viewer->loaded = true;
    }
    fs_io_release(request);
    viewer->request = NULL;
    wm_invalidate(viewer->window);
}

static void image_viewer_on_close(window_t *win)
{
    image_viewer_t *viewer = win->priv;
    if (viewer) {
        if (viewer->request) {
            fs_io_cancel(viewer->request);
            fs_io_release(viewer->request);
        }
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
            widget_text_centered(client, viewer->request ? "Loading..." :
                                viewer->error, viewer->request ? COLOR_TEXT :
                                COLOR_DANGER);
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
