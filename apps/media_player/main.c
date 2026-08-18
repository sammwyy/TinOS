#include <tinos/abi.h>
#include <tinos/ui.h>

#include "../lib/shim.h"

window_t *app_media_player_open(const char *arg);

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
        .launch      = app_media_player_open,
    });
}
