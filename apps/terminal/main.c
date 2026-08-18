#include <tinos/abi.h>
#include <tinos/ui.h>

#include "../lib/shim.h"

window_t *app_terminal_open(const char *cwd);

void app_main(const tinos_abi_t *abi)
{
    tinos_abi_install(abi);
    apps_register(&(app_entry_t){
        .name     = "Terminal",
        .summary  = "command line",
        .category = "System",
        .icon     = ICON_TERMINAL,
        .launch   = app_terminal_open,
    });
}
