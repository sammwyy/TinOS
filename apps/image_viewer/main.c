#include <tinos/abi.h>
#include <tinos/ui.h>

#include "../lib/shim.h"

window_t *app_image_viewer_open(const char *arg);

void app_main(const tinos_abi_t *abi)
{
    tinos_abi_install(abi);
    apps_register(&(app_entry_t){
        .name     = "Image Viewer",
        .summary  = "view JPG, PNG and WebP images",
        .category = "Multimedia",
        .icon     = ICON_FILE,
        .launch   = app_image_viewer_open,
    });
}
