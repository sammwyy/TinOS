#ifndef TINOS_DYNLINK_H
#define TINOS_DYNLINK_H

#include <tinos/types.h>
#include <tinos/user_vm.h>

#define DYNLINK_INIT_LIMIT 32u
#define DYNLINK_FINI_LIMIT 32u
#define DYNLINK_LIBRARY_LIMIT 8u

typedef struct {
    uintptr_t address;
    uint8_t slot;
} dynlink_finalizer_t;

typedef struct {
    uint32_t mask;
    uint32_t needed_roots;
    uint32_t dependencies[DYNLINK_LIBRARY_LIMIT];
    uint8_t opens[DYNLINK_LIBRARY_LIMIT];
    uint32_t tls_used;
    uint32_t tls_end[DYNLINK_LIBRARY_LIMIT];
    uint32_t tls_size[DYNLINK_LIBRARY_LIMIT];
    uintptr_t initializers[DYNLINK_INIT_LIMIT];
    unsigned next_initializer;
    unsigned initializer_count;
    dynlink_finalizer_t finalizers[DYNLINK_FINI_LIMIT];
    unsigned finalizer_count;
    uint32_t close_mask;
    unsigned close_cursor;
    int close_handle;
} dynlink_state_t;

/* Called with the target user address space active, before entering ring 3. */
bool dynlink_load(const void *image, size_t size, user_space_t *space,
                  dynlink_state_t *state);
int dynlink_open(const char *name, user_space_t *space, dynlink_state_t *state);
int dynlink_close(int handle, user_space_t *space, dynlink_state_t *state);
int dynlink_prepare_close(int handle, dynlink_state_t *state);
uintptr_t dynlink_close_next_finalizer(dynlink_state_t *state);
uintptr_t dynlink_symbol(int handle, const char *name,
                         const user_space_t *space,
                         const dynlink_state_t *state);
uintptr_t dynlink_next_initializer(dynlink_state_t *state);
uintptr_t dynlink_next_finalizer(dynlink_state_t *state);

#endif
