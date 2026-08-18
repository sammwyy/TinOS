#include <tinos/fs.h>
#include <tinos/heap.h>
#include <tinos/image.h>
#include <tinos/io.h>
#include <tinos/printf.h>
#include <tinos/spinlock.h>
#include <tinos/string.h>
#include <tinos/thread.h>
#include <tinos/ui.h>

typedef struct {
    image_t image;
    char error[64];
    bool loaded;
    bool decode_done;
    bool closing;
    /* Non-NULL while a read or a decode is in flight; NULL once one of
     * them has finished (successfully, on error, or cancelled). Decoding
     * runs on its own kernel thread, which any SMP core may run, so these
     * fields (touched by both that thread and the desktop's main loop)
     * need a real cross-core lock - irq_save()/irq_restore() only ever
     * stopped the calling core's own interrupts. */
    spinlock_t lock;
    fs_io_request_t *request;
    kernel_thread_t *decode_worker;
    char extension[16];
    window_t *window;
} image_viewer_t;

#define VIEWER_FOOTER_H  28

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
    spin_init(&viewer->lock);

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

/* Decoding runs on its own kernel thread so a slow JPEG/PNG/WebP does not
 * stall the desktop loop - the PIT preempts it like any other thread. */
static void image_viewer_decode_worker(void *arg)
{
    image_viewer_t *viewer = arg;
    fs_io_request_t *request = viewer->request;
    if (!thread_cancelled()) {
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

    bool saved = spin_lock_irqsave(&viewer->lock);
    viewer->request = NULL;
    viewer->decode_done = true;
    bool destroy = viewer->closing;
    spin_unlock_irqrestore(&viewer->lock, saved);
    if (destroy) {
        image_destroy(&viewer->image);
        kfree(viewer);
    }
    thread_exit();
}

static void image_viewer_loaded(fs_io_request_t *request, void *arg)
{
    image_viewer_t *viewer = arg;
    if (!fs_io_ok(request)) {
        strlcpy(viewer->error, "Unable to read file", sizeof(viewer->error));
        fs_io_release(request);
        viewer->request = NULL;
        wm_invalidate(viewer->window);
        return;
    }
    viewer->decode_worker = thread_spawn("image-decode",
                                         image_viewer_decode_worker, viewer);
    if (!viewer->decode_worker) {
        strlcpy(viewer->error, "Out of memory", sizeof(viewer->error));
        fs_io_release(request);
        viewer->request = NULL;
        wm_invalidate(viewer->window);
    }
    /* On success the worker now owns `request` and releases it when done. */
}

static bool image_viewer_on_tick(window_t *win)
{
    image_viewer_t *viewer = win->priv;
    if (!viewer)
        return false;
    bool saved = spin_lock_irqsave(&viewer->lock);
    bool done = viewer->decode_done;
    viewer->decode_done = false;
    spin_unlock_irqrestore(&viewer->lock, saved);
    return done;
}

static void image_viewer_on_close(window_t *win)
{
    image_viewer_t *viewer = win->priv;
    win->priv = NULL;
    if (!viewer)
        return;

    if (viewer->decode_worker) {
        /* The worker owns `request` now; it releases it and (since
         * `closing` will be set) frees `viewer` itself once it finishes. */
        thread_cancel(viewer->decode_worker);
        thread_detach(viewer->decode_worker);
        bool saved = spin_lock_irqsave(&viewer->lock);
        viewer->closing = true;
        bool destroy_now = viewer->request == NULL;
        spin_unlock_irqrestore(&viewer->lock, saved);
        if (destroy_now) {
            image_destroy(&viewer->image);
            kfree(viewer);
        }
        return;
    }

    if (viewer->request) {
        fs_io_cancel(viewer->request);
        fs_io_release(viewer->request);
    }
    image_destroy(&viewer->image);
    kfree(viewer);
}

static void image_viewer_on_draw(window_t *win, rect_t client)
{
    image_viewer_t *viewer = win->priv;
    gfx_fill_rect(client.x, client.y, client.w, client.h, RGB(0x20, 0x25, 0x30));
    if (!viewer || !viewer->loaded) {
        if (viewer) {
            rect_t empty = rect_make(client.x, client.y,
                                     client.w, client.h - VIEWER_FOOTER_H);
            widget_text_centered(empty, viewer->request ? "Loading..." :
                                viewer->error, viewer->request ? COLOR_TEXT :
                                COLOR_DANGER);
        }
        return;
    }

    int max_w = client.w - 32;
    int max_h = client.h - VIEWER_FOOTER_H - 20;
    int draw_w = max_w;
    int draw_h = (int)((uint64_t)max_w * viewer->image.height /
                       viewer->image.width);
    if (draw_h > max_h) {
        draw_h = max_h;
        draw_w = (int)((uint64_t)max_h * viewer->image.width /
                       viewer->image.height);
    }
    rect_t image_area = rect_make(client.x + 16, client.y + 8,
                                  max_w, max_h);
    rect_t image = rect_make(image_area.x + (image_area.w - draw_w) / 2,
                             image_area.y + (image_area.h - draw_h) / 2,
                             draw_w, draw_h);
    gfx_fill_rect(image_area.x, image_area.y, image_area.w, image_area.h,
                  RGB(0x20, 0x25, 0x30));
    for (int y = image.y; y < image.y + image.h; y += 16)
        for (int x = image.x; x < image.x + image.w; x += 16)
            gfx_fill_rect(x, y, MIN(16, image.x + image.w - x),
                          MIN(16, image.y + image.h - y),
                          ((x / 16 + y / 16) & 1) ? RGB(0x9a, 0x9a, 0xa2)
                                                  : RGB(0xe1, 0xe1, 0xe5));
    gfx_rect(image.x - 1, image.y - 1, image.w + 2, image.h + 2,
             RGB(0x4c, 0x5b, 0x72));
    gfx_blit_image(image, viewer->image.pixels, (int)viewer->image.width,
                   (int)viewer->image.height);

    char info[64];
    ksnprintf(info, sizeof(info), "%u x %u", viewer->image.width,
               viewer->image.height);
    gfx_text(client.x + 12, client.y + client.h - FONT_H - 7, info,
             RGB(0xe0, 0xe3, 0xeb));
    gfx_text(client.x + client.w - 104, client.y + client.h - FONT_H - 7,
             "Fit to window", COLOR_TEXT_DIM);
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
    .on_tick = image_viewer_on_tick,
};

window_t *app_image_viewer_open(const char *path)
{
    return wm_open(&image_viewer_class, 760, 540, "Image Viewer", path);
}
