#include <stdint.h>
#include <tinos/tls.h>

typedef struct {
    uint32_t module;
    uint32_t offset;
} tls_index_t;

__attribute__((regparm(1))) void *___tls_get_addr(const tls_index_t *index)
{
    uint32_t *tcb;
    __asm__ volatile("movl %%gs:0, %0" : "=r"(tcb));
    if (!index || index->module < 1 || index->module > TINOS_TLS_MODULES)
        return 0;
    unsigned slot = index->module - 1;
    uint32_t base = tcb[TINOS_TLS_BASE_WORD(slot)];
    uint32_t size = tcb[TINOS_TLS_SIZE_WORD(slot)];
    if (!base || index->offset >= size)
        return 0;
    return (void *)(uintptr_t)(base + index->offset);
}
