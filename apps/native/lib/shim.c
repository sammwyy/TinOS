/* Forwards every call a native app makes to the real kernel function of the
 * same name through the ABI table the loader handed to app_main(). App
 * source code stays unchanged: it still calls wm_open(), gfx_text() and so
 * on directly, it just links against this instead of the real kernel
 * objects.
 */
#include <tinos/abi.h>

static const tinos_abi_t *abi;

void tinos_abi_install(const tinos_abi_t *table)
{
    abi = table;
}

window_t *wm_open(const app_class_t *cls, int width, int height,
                  const char *title, const char *arg)
{
    return abi->wm_open(cls, width, height, title, arg);
}

void wm_close(window_t *win)
{
    abi->wm_close(win);
}

void wm_invalidate(window_t *win)
{
    abi->wm_invalidate(win);
}

void gfx_fill_rect(int x, int y, int w, int h, color_t color)
{
    abi->gfx_fill_rect(x, y, w, h, color);
}

void gfx_text(int x, int y, const char *s, color_t fg)
{
    abi->gfx_text(x, y, s, fg);
}

int gfx_text_width(const char *s)
{
    return abi->gfx_text_width(s);
}

void widget_panel(rect_t r, color_t fill, color_t border)
{
    abi->widget_panel(r, fill, border);
}

void widget_text_centered(rect_t r, const char *text, color_t color)
{
    abi->widget_text_centered(r, text, color);
}

void widget_button(rect_t r, const char *label, bool hot, bool pressed)
{
    abi->widget_button(r, label, hot, pressed);
}

bool apps_register(const app_entry_t *entry)
{
    return abi->apps_register(entry);
}

void *kmalloc(size_t size)
{
    return abi->kmalloc(size);
}

void *kcalloc(size_t count, size_t size)
{
    return abi->kcalloc(count, size);
}

void *krealloc(void *ptr, size_t size)
{
    return abi->krealloc(ptr, size);
}

void kfree(void *ptr)
{
    abi->kfree(ptr);
}
