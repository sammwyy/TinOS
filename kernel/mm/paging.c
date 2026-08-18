/* Identity-mapped paging.
 *
 * Every virtual address maps to the identical physical address, so nothing
 * outside this file changes: pmm_alloc_frame() results, heap pointers, DMA
 * buffers and MMIO BARs (the framebuffer, PCI device registers) all keep
 * meaning exactly what they meant before paging was turned on.
 *
 * The whole 4 GiB address space is covered by a single page directory of
 * 4 MiB pages (CR4.PSE) rather than a two-level 4 KiB page table tree, so
 * there is no page table allocation to get wrong and no address can ever be
 * accidentally left unmapped.
 */

#include <tinos/kernel.h>
#include <tinos/mm.h>

#define PDE_PRESENT   (1u << 0)
#define PDE_WRITABLE  (1u << 1)
#define PDE_PAGE_SIZE (1u << 7) /* PS: this entry maps a 4 MiB page directly */

/* Must be page (4 KiB) aligned - this address is loaded straight into CR3. */
static uint32_t page_directory[1024] __attribute__((aligned(4096)));

/* CR0/CR3/CR4 are per-CPU control registers, so every core - the boot
 * processor and every SMP application processor - must run this on itself;
 * only the table contents (built once, by paging_init()) are shared. */
void paging_activate(void)
{
    uint32_t cr4;
    __asm__ volatile("movl %%cr4, %0" : "=r"(cr4));
    cr4 |= (1u << 4); /* PSE: 4 MiB pages */
    __asm__ volatile("movl %0, %%cr4" : : "r"(cr4));

    __asm__ volatile("movl %0, %%cr3" : : "r"(page_directory));

    uint32_t cr0;
    __asm__ volatile("movl %%cr0, %0" : "=r"(cr0));
    cr0 |= (1u << 31) | (1u << 16); /* PG: enable paging, WP: honour RO pages */
    __asm__ volatile("movl %0, %%cr0" : : "r"(cr0));
}

void paging_init(void)
{
    for (uint32_t i = 0; i < 1024; i++)
        page_directory[i] = (i << 22) | PDE_PRESENT | PDE_WRITABLE |
                            PDE_PAGE_SIZE;
    paging_activate();
}
