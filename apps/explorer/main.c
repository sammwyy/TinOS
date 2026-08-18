#include <tinos/abi.h>
#include <tinos/fs.h>
#include <tinos/ui.h>

#include "../lib/shim.h"

window_t *app_explorer_open(const char *arg);

static window_t *launch_explorer(const char *arg)
{
    return app_explorer_open(arg ? arg : TINOS_DEFAULT_HOME);
}

void app_main(const tinos_abi_t *abi)
{
    tinos_abi_install(abi);
    apps_register(&(app_entry_t){
        .name     = "File Explorer",
        .summary  = "browse files",
        .category = "Accessories",
        .icon     = ICON_FOLDER,
        .launch   = launch_explorer,
    });
}
