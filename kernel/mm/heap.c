/* Kernel heap.
 *
 * A first-fit allocator over one or more arenas.  Each arena is a run of
 * contiguous page frames obtained from the physical allocator and carved up
 * into an address-ordered doubly linked list of blocks; neighbouring free
 * blocks are coalesced on release.  When an allocation cannot be satisfied a
 * new arena is requested from the PMM, so the heap grows on demand instead
 * of being fixed at boot.
 */

#include <tinos/heap.h>
#include <tinos/kernel.h>
#include <tinos/mm.h>
#include <tinos/string.h>

#define HEAP_MAGIC      0x4E4F5641u /* "TINOS" */
#define HEAP_ALIGN      8u
#define ARENA_MIN_PAGES 1024u       /* 4 MiB growth increments */

typedef struct block {
    uint32_t      magic;
    size_t        size; /* payload bytes */
    bool          free;
    struct block *prev;
    struct block *next;
} block_t;

typedef struct arena {
    struct arena *next;
    size_t        pages;
    block_t      *first;
} arena_t;

static arena_t *arenas;
static size_t   stat_total;
static size_t   stat_used;
static size_t   stat_allocs;
static size_t   stat_frees;

static const size_t block_overhead = ALIGN_UP(sizeof(block_t), HEAP_ALIGN);

static arena_t *arena_create(size_t min_payload)
{
    size_t need  = min_payload + block_overhead + ALIGN_UP(sizeof(arena_t), HEAP_ALIGN);
    size_t pages = ALIGN_UP(need, PAGE_SIZE) / PAGE_SIZE;
    if (pages < ARENA_MIN_PAGES)
        pages = ARENA_MIN_PAGES;

    void *mem = pmm_alloc_contig(pages);
    if (!mem && pages > ARENA_MIN_PAGES) {
        return NULL; /* a huge request that physical memory cannot satisfy */
    }
    while (!mem && pages > 16) {
        pages /= 2;
        if (pages * PAGE_SIZE < need)
            return NULL;
        mem = pmm_alloc_contig(pages);
    }
    if (!mem)
        return NULL;

    arena_t *arena = (arena_t *)mem;
    arena->pages   = pages;
    arena->next    = arenas;

    uintptr_t base = (uintptr_t)mem + ALIGN_UP(sizeof(arena_t), HEAP_ALIGN);
    size_t    span = pages * PAGE_SIZE - ALIGN_UP(sizeof(arena_t), HEAP_ALIGN);

    block_t *b = (block_t *)base;
    b->magic   = HEAP_MAGIC;
    b->size    = span - block_overhead;
    b->free    = true;
    b->prev    = NULL;
    b->next    = NULL;
    arena->first = b;

    arenas = arena;
    stat_total += b->size;
    return arena;
}

void heap_init(void)
{
    if (!arena_create(0))
        panic("heap: unable to reserve the initial arena");
}

static void *carve(block_t *b, size_t size)
{
    /* Split the tail off when the remainder can hold a usable block. */
    if (b->size >= size + block_overhead + HEAP_ALIGN * 4) {
        block_t *tail = (block_t *)((uintptr_t)b + block_overhead + size);
        tail->magic   = HEAP_MAGIC;
        tail->size    = b->size - size - block_overhead;
        tail->free    = true;
        tail->prev    = b;
        tail->next    = b->next;
        if (tail->next)
            tail->next->prev = tail;
        b->next = tail;
        b->size = size;
        stat_total -= block_overhead;
    }
    b->free = false;
    stat_used += b->size;
    stat_allocs++;
    return (void *)((uintptr_t)b + block_overhead);
}

void *kmalloc(size_t size)
{
    if (size == 0)
        size = 1;
    size = ALIGN_UP(size, HEAP_ALIGN);

    for (arena_t *a = arenas; a; a = a->next)
        for (block_t *b = a->first; b; b = b->next)
            if (b->free && b->size >= size)
                return carve(b, size);

    /* Nothing large enough: grow the heap. */
    arena_t *a = arena_create(size);
    if (!a)
        return NULL;
    return carve(a->first, size);
}

void *kcalloc(size_t count, size_t size)
{
    size_t total = count * size;
    if (count && total / count != size)
        return NULL; /* overflow */
    void *p = kmalloc(total);
    if (p)
        memset(p, 0, total);
    return p;
}

void kfree(void *ptr)
{
    if (!ptr)
        return;

    block_t *b = (block_t *)((uintptr_t)ptr - block_overhead);
    if (b->magic != HEAP_MAGIC)
        panic("heap: kfree(%p) on a corrupted or foreign block", ptr);
    if (b->free)
        panic("heap: double free of %p", ptr);

    b->free = true;
    stat_used -= b->size;
    stat_frees++;

    if (b->next && b->next->free) {
        block_t *n = b->next;
        b->size += n->size + block_overhead;
        b->next = n->next;
        if (n->next)
            n->next->prev = b;
        stat_total += block_overhead;
        n->magic = 0;
    }
    if (b->prev && b->prev->free) {
        block_t *p = b->prev;
        p->size += b->size + block_overhead;
        p->next = b->next;
        if (b->next)
            b->next->prev = p;
        stat_total += block_overhead;
        b->magic = 0;
    }
}

void *krealloc(void *ptr, size_t size)
{
    if (!ptr)
        return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    block_t *b = (block_t *)((uintptr_t)ptr - block_overhead);
    if (b->magic != HEAP_MAGIC)
        panic("heap: krealloc(%p) on a corrupted or foreign block", ptr);

    size_t want = ALIGN_UP(size, HEAP_ALIGN);
    if (b->size >= want)
        return ptr;

    /* Absorb the following block when it is free and large enough. */
    if (b->next && b->next->free &&
        b->size + block_overhead + b->next->size >= want) {
        block_t *n = b->next;
        stat_used -= b->size;
        b->size += n->size + block_overhead;
        b->next = n->next;
        if (n->next)
            n->next->prev = b;
        n->magic = 0;
        stat_total += block_overhead;
        stat_used += b->size;
        return ptr;
    }

    void *fresh = kmalloc(size);
    if (!fresh)
        return NULL;
    memcpy(fresh, ptr, b->size < size ? b->size : size);
    kfree(ptr);
    return fresh;
}

void heap_get_stats(heap_stats_t *out)
{
    out->heap_total  = stat_total;
    out->heap_used   = stat_used;
    out->heap_free   = stat_total - stat_used;
    out->alloc_count = stat_allocs;
    out->free_count  = stat_frees;
}
