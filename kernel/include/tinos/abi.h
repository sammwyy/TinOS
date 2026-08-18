/* The function table a loaded native app's entry point receives.
 *
 * Native apps are linked as separate ELF binaries (see apps/native/), so
 * they cannot call kernel functions by symbol the way the still-builtin
 * apps in apps/ do - the kernel binary and the app binary never see each
 * other's symbol tables. apps/native/lib/shim.c wraps every entry here in a
 * function of the same name as the real kernel call, so app source code
 * stays unchanged; only its build (linked against the shim instead of the
 * real kernel objects) is different.
 *
 * Grow this table only for calls an actual native app needs - it is the
 * one thing that has to stay in sync between the kernel and every native
 * app's build.
 */
#ifndef TINOS_ABI_H
#define TINOS_ABI_H

#include <tinos/gfx.h>
#include <tinos/types.h>
#include <tinos/ui.h>

typedef struct {
    window_t *(*wm_open)(const app_class_t *cls, int width, int height,
                         const char *title, const char *arg);
    void (*wm_close)(window_t *win);
    void (*wm_invalidate)(window_t *win);

    void (*gfx_fill_rect)(int x, int y, int w, int h, color_t color);
    void (*gfx_text)(int x, int y, const char *s, color_t fg);
    int  (*gfx_text_width)(const char *s);

    void (*widget_panel)(rect_t r, color_t fill, color_t border);
    void (*widget_text_centered)(rect_t r, const char *text, color_t color);
    void (*widget_button)(rect_t r, const char *label, bool hot,
                          bool pressed);

    bool (*apps_register)(const app_entry_t *entry);

    void *(*kmalloc)(size_t size);
    void *(*kcalloc)(size_t count, size_t size);
    void *(*krealloc)(void *ptr, size_t size);
    void  (*kfree)(void *ptr);
} tinos_abi_t;

/* Populated with the kernel's own current addresses; safe to call once,
 * any time after the subsystems above are up. */
tinos_abi_t tinos_abi_build(void);

#endif /* TINOS_ABI_H */
