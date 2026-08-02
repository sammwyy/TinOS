/* Kernel heap.
 *
 * The host test harness provides malloc-backed implementations of these so
 * the filesystem and shell can be exercised natively.
 */
#ifndef TINOS_HEAP_H
#define TINOS_HEAP_H

#include <tinos/types.h>

void *kmalloc(size_t size);
void *kcalloc(size_t count, size_t size);
void *krealloc(void *ptr, size_t size);
void  kfree(void *ptr);

/* Statistics for the system-information application. */
typedef struct {
    size_t heap_total;
    size_t heap_used;
    size_t heap_free;
    size_t alloc_count;
    size_t free_count;
} heap_stats_t;

void heap_get_stats(heap_stats_t *out);

#endif /* TINOS_HEAP_H */
