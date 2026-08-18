/* Entry point of the standalone calculator.elf. The linker script points
 * e_entry straight at app_main(), so the loader can call it with no crt0
 * stub in between. */
#include <tinos/abi.h>
#include <tinos/ui.h>

#include "../lib/shim.h"

window_t *app_calculator_open(const char *arg);

void app_main(const tinos_abi_t *abi)
{
    tinos_abi_install(abi);
    apps_register(&(app_entry_t){
        .name     = "Calculator",
        .summary  = "integer arithmetic",
        .category = "Accessories",
        .icon     = ICON_CHIP,
        .launch   = app_calculator_open,
    });
}
