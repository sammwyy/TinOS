#include <tinos/abi.h>
#include <tinos/heap.h>

tinos_abi_t tinos_abi_build(void)
{
    return (tinos_abi_t){
        .wm_open              = wm_open,
        .wm_close             = wm_close,
        .wm_invalidate        = wm_invalidate,
        .gfx_fill_rect        = gfx_fill_rect,
        .gfx_text             = gfx_text,
        .gfx_text_width       = gfx_text_width,
        .widget_panel         = widget_panel,
        .widget_text_centered = widget_text_centered,
        .widget_button        = widget_button,
        .apps_register        = apps_register,
        .kmalloc              = kmalloc,
        .kcalloc              = kcalloc,
        .krealloc             = krealloc,
        .kfree                = kfree,
    };
}
