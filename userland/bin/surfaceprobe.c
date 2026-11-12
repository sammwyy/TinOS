#include <tinos/libc.h>
#include <tinos/string.h>

int main(void)
{
    int handle = tinos_gui_open("Surface test", 240, 180);
    if (handle < 0)
        return 1;
    tinos_gui_size_t size;
    tinos_gui_theme_t theme;
    uint32_t pixel = 0x00335577;
    int ok = tinos_gui_surface_size(handle, &size) == 0 &&
             size.width > 32 && size.height > 32 &&
             tinos_gui_theme(&theme) == 0 && theme.revision == 1 &&
             tinos_gui_surface_size(handle,
                    (tinos_gui_size_t *)0x00100000u) == -1 &&
             tinos_gui_surface_config(handle, 0, 32) == -1 &&
             tinos_gui_surface_config(handle, size.width + 1, 32) == -1 &&
             tinos_gui_surface_config(handle, 32, 32) == 0 &&
             tinos_gui_surface_tile(handle, 0, 0, 1, 1,
                                   (const uint32_t *)0x00100000u) == -1 &&
             tinos_gui_surface_tile(handle, 32, 0, 1, 1, &pixel) == -1 &&
             tinos_gui_surface_tile(handle, 0xffffffffu, 0, 1, 1,
                                   &pixel) == -1 &&
             tinos_gui_surface_tile(handle, 0, 0, 65, 1, &pixel) == -1 &&
             tinos_gui_surface_tile(handle + 1, 0, 0, 1, 1, &pixel) == -1 &&
             tinos_gui_surface_tile(handle, 0, 0, 1, 1, &pixel) == 0 &&
             tinos_gui_close(handle) == 0 &&
             tinos_gui_surface_tile(handle, 0, 0, 1, 1, &pixel) == -1;
    if (!ok)
        tinos_gui_close(handle);
    const char *message = ok ? "ring3: surface bounds and theme isolated\n" :
                               "ring3: surface isolation FAILED\n";
    tinos_write(1, message, strlen(message));
    return ok ? 0 : 1;
}
