#include <tinos/abi.h>
#include <tinos/ui.h>

#include "../lib/shim.h"

window_t *app_editor_open(const char *arg);

static window_t *launch_editor(const char *arg)
{
    return app_editor_open(arg ? arg : "/README.txt");
}

void app_main(const tinos_abi_t *abi, const char *name, const char *summary,
             const char *category, icon_t icon, image_t custom_icon)
{
    tinos_abi_install(abi);
    apps_register(&(app_entry_t){
        .name        = name,
        .summary     = summary,
        .category    = category,
        .icon        = icon,
        .custom_icon = custom_icon,
        .launch      = launch_editor,
    });
}
