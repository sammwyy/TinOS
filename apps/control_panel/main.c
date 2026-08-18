#include <tinos/abi.h>
#include <tinos/ui.h>

#include "../lib/shim.h"

window_t *app_control_panel_open(const char *arg);

void app_main(const tinos_abi_t *abi)
{
    tinos_abi_install(abi);
    apps_register(&(app_entry_t){
        .name     = "Control Panel",
        .summary  = "network and system settings",
        .category = "System",
        .icon     = ICON_CHIP,
        .launch   = app_control_panel_open,
    });
}
