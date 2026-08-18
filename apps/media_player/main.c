#include <tinos/abi.h>
#include <tinos/ui.h>

#include "../lib/shim.h"

window_t *app_media_player_open(const char *arg);

void app_main(const tinos_abi_t *abi)
{
    tinos_abi_install(abi);
    apps_register(&(app_entry_t){
        .name     = "Media Player",
        .summary  = "play WAV and Ogg Vorbis audio",
        .category = "Multimedia",
        .icon     = ICON_FILE,
        .launch   = app_media_player_open,
    });
}
