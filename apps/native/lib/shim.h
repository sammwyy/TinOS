#ifndef TINOS_NATIVE_SHIM_H
#define TINOS_NATIVE_SHIM_H

#include <tinos/abi.h>

/* Must be the first thing app_main() does: every other wrapper in shim.c
 * forwards through the table stashed here. */
void tinos_abi_install(const tinos_abi_t *table);

#endif /* TINOS_NATIVE_SHIM_H */
