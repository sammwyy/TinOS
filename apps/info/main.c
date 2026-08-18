#include <tinos/abi.h>
#include <tinos/ui.h>

#include "../lib/shim.h"

window_t *app_about_open(const char *arg);
window_t *app_sysinfo_open(const char *arg);

void app_main(const tinos_abi_t *abi)
{
    tinos_abi_install(abi);
    apps_register(&(app_entry_t){
        .name     = "About TinOS",
        .summary  = "version and info",
        .category = "System",
        .icon     = ICON_INFO,
        .launch   = app_about_open,
    });
    apps_register(&(app_entry_t){
        .name     = "System Information",
        .summary  = "system details",
        .category = "System",
        .icon     = ICON_CHIP,
        .launch   = app_sysinfo_open,
    });
}
