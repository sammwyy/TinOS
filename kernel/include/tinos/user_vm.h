/* i386 user address space. The kernel remains supervisor-only identity mapped. */
#ifndef TINOS_USER_VM_H
#define TINOS_USER_VM_H

#include <tinos/types.h>
#include <uapi/tinos/tls.h>

#define USER_BASE       0x40000000u
#define USER_REGION_END 0x40400000u
#define USER_STACK_TOP  USER_REGION_END
#define USER_TLS_BASE   (USER_STACK_TOP - 3 * 4096u)
#define USER_TLS_TOP    (USER_TLS_BASE + 4096u - TINOS_TLS_TCB_BYTES)
#define USER_TLS_LIMIT  (USER_TLS_TOP - USER_TLS_BASE)

typedef struct {
    uint32_t *directory;
    uint32_t *table;
} user_space_t;

bool user_space_init(user_space_t *space);
bool user_space_map(user_space_t *space, uintptr_t address);
/* A pinned supervisor-owned frame mapped read-only in this process. */
bool user_space_map_shared(user_space_t *space, uintptr_t address, void *frame);
void user_space_unmap(user_space_t *space, uintptr_t address);
void user_space_readonly(user_space_t *space, uintptr_t address);
void user_space_activate(const user_space_t *space);
void user_space_kernel(void);
void user_space_destroy(user_space_t *space);
/* Number of present user pages, shared mappings included. */
size_t user_space_pages(const user_space_t *space);

#endif /* TINOS_USER_VM_H */
