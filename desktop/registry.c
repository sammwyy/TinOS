/* Application registry: drives the start menu. */
#include <tinos/ui.h>

#define REGISTRY_CAPACITY 16

static app_entry_t registry[REGISTRY_CAPACITY];
static size_t       registry_count;

const app_entry_t *apps_registry(size_t *count)
{
    *count = registry_count;
    return registry;
}

bool apps_register(const app_entry_t *entry)
{
    if (registry_count >= REGISTRY_CAPACITY)
        return false;
    registry[registry_count++] = *entry;
    return true;
}

void app_icon_draw(int x, int y, int size, const app_entry_t *app)
{
    if (app->custom_icon.pixels)
        gfx_blit_image(rect_make(x, y, size, size), app->custom_icon.pixels,
                       (int)app->custom_icon.width,
                       (int)app->custom_icon.height);
    else
        widget_icon(x, y, size, app->icon);
}
