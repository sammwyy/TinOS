#ifndef TINOS_DYNLINK_H
#define TINOS_DYNLINK_H

#include <tinos/types.h>
#include <tinos/user_vm.h>

/* Called with the target user address space active, before entering ring 3. */
bool dynlink_load(const void *image, size_t size, user_space_t *space,
                  uint32_t *library_mask);
int dynlink_open(const char *name, user_space_t *space, uint32_t *library_mask);
uintptr_t dynlink_symbol(int handle, const char *name,
                         const user_space_t *space, uint32_t library_mask);

#endif
