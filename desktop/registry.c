/* Userspace executable metadata for the start menu. */
#include <tinos/fs.h>
#include <tinos/string.h>
#include <tinos/ui.h>

#define REGISTRY_CAPACITY 16

static app_entry_t registry[REGISTRY_CAPACITY];
static size_t       registry_count;
static char         registry_names[REGISTRY_CAPACITY][FS_NAME_MAX];
static char         registry_paths[REGISTRY_CAPACITY][sizeof("/apps/gui/") + FS_NAME_MAX];

const app_entry_t *apps_registry(size_t *count)
{
    *count = registry_count;
    return registry;
}

void apps_discover(void)
{
    registry_count = 0;
    fs_node_t *dir;
    if (fs_resolve(fs_root(), "/apps/gui", &dir) != FS_OK || dir->type != FS_DIR)
        return;
    for (fs_node_t *file = dir->children; file && registry_count < REGISTRY_CAPACITY;
         file = file->next) {
        size_t length = strlen(file->name);
        if (file->type != FS_FILE || !file->data || length <= 4 ||
            strcmp(file->name + length - 4, ".elf") != 0)
            continue;
        size_t slot = registry_count++;
        strlcpy(registry_names[slot], file->name, sizeof(registry_names[slot]));
        strlcpy(registry_paths[slot], "/apps/gui/", sizeof(registry_paths[slot]));
        strlcpy(registry_paths[slot] + sizeof("/apps/gui/") - 1, file->name,
                sizeof(registry_paths[slot]) - (sizeof("/apps/gui/") - 1));
        registry[slot] = (app_entry_t){
            .name = registry_names[slot],
            .summary = "",
            .category = "Programs",
            .path = registry_paths[slot],
            .icon = ICON_FILE,
        };
    }
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
