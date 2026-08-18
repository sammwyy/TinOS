/* Physical memory manager. */
#ifndef TINOS_MM_H
#define TINOS_MM_H

#include <tinos/multiboot2.h>
#include <tinos/types.h>

#define PAGE_SIZE 4096u

void  pmm_init(const mb_info_t *mbi);
void *pmm_alloc_frame(void);
void *pmm_alloc_contig(size_t frames);
void  pmm_free_frame(void *frame);
void  pmm_free_contig(void *base, size_t frames);
void  pmm_reserve_range(uintptr_t base, size_t length);

uint64_t pmm_total_bytes(void);
uint64_t pmm_free_bytes(void);
uint64_t pmm_used_bytes(void);
size_t   pmm_region_count(void);

/* Kernel heap bootstrap (implemented in mm/heap.c). */
void heap_init(void);

/* Identity-mapped paging (implemented in mm/paging.c). Physical and virtual
 * addresses stay equal, so nothing else needs to change: it exists so an
 * SMP application processor can be handed a valid CR3 during its own
 * bring-up, and so the kernel is no longer permanently committed to the
 * "no paging" design. */
void paging_init(void);

#endif /* TINOS_MM_H */
