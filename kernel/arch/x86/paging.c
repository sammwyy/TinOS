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
#include <tinos/string.h>
#include <tinos/user_vm.h>

#define PDE_PRESENT   (1u << 0)
#define PDE_WRITABLE  (1u << 1)
#define PDE_USER      (1u << 2)
#define PDE_PAGE_SIZE (1u << 7) /* PS: this entry maps a 4 MiB page directly */
#define PTE_SHARED    (1u << 9)
#define USER_PDE_INDEX (USER_BASE >> 22)

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

bool user_space_init(user_space_t *space)
{
    space->directory = pmm_alloc_frame();
    if (!space->directory)
        return false;
    space->table = pmm_alloc_frame();
    if (!space->table) {
        pmm_free_frame(space->directory);
        space->directory = NULL;
        return false;
    }
    memcpy(space->directory, page_directory, PAGE_SIZE);
    memset(space->table, 0, PAGE_SIZE);
    space->directory[USER_PDE_INDEX] = (uint32_t)(uintptr_t)space->table |
                                      PDE_PRESENT | PDE_WRITABLE | PDE_USER;
    return true;
}

bool user_space_map(user_space_t *space, uintptr_t address)
{
    if (address < USER_BASE || address >= USER_REGION_END ||
        (address & (PAGE_SIZE - 1)))
        return false;
    size_t index = (address - USER_BASE) / PAGE_SIZE;
    if (space->table[index] & PDE_PRESENT)
        return false;
    void *frame = pmm_alloc_frame();
    if (!frame)
        return false;
    memset(frame, 0, PAGE_SIZE);
    space->table[index] = (uint32_t)(uintptr_t)frame |
                          PDE_PRESENT | PDE_WRITABLE | PDE_USER;
    __asm__ volatile("invlpg (%0)" : : "r"(address) : "memory");
    return true;
}

bool user_space_map_shared(user_space_t *space, uintptr_t address, void *frame)
{
    if (!frame || ((uintptr_t)frame & (PAGE_SIZE - 1)) ||
        address < USER_BASE || address >= USER_REGION_END ||
        (address & (PAGE_SIZE - 1)))
        return false;
    size_t index = (address - USER_BASE) / PAGE_SIZE;
    if (space->table[index] & PDE_PRESENT)
        return false;
    space->table[index] = (uint32_t)(uintptr_t)frame |
                          PDE_PRESENT | PDE_USER | PTE_SHARED;
    __asm__ volatile("invlpg (%0)" : : "r"(address) : "memory");
    return true;
}

void user_space_unmap(user_space_t *space, uintptr_t address)
{
    if (address < USER_BASE || address >= USER_REGION_END ||
        (address & (PAGE_SIZE - 1)))
        return;
    size_t index = (address - USER_BASE) / PAGE_SIZE;
    uint32_t entry = space->table[index];
    if (!(entry & PDE_PRESENT))
        return;
    space->table[index] = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(address) : "memory");
    if (!(entry & PTE_SHARED))
        pmm_free_frame((void *)(uintptr_t)(entry & ~0xfffu));
}

void user_space_readonly(user_space_t *space, uintptr_t address)
{
    size_t index = (address - USER_BASE) / PAGE_SIZE;
    space->table[index] &= ~PDE_WRITABLE;
    __asm__ volatile("invlpg (%0)" : : "r"(address) : "memory");
}

void user_space_activate(const user_space_t *space)
{
    __asm__ volatile("movl %0, %%cr3" : : "r"(space->directory) : "memory");
}

void user_space_kernel(void)
{
    __asm__ volatile("movl %0, %%cr3" : : "r"(page_directory) : "memory");
}

void user_space_destroy(user_space_t *space)
{
    if (!space->directory)
        return;
    for (size_t i = 0; i < 1024; i++)
        if ((space->table[i] & (PDE_PRESENT | PTE_SHARED)) == PDE_PRESENT)
            pmm_free_frame((void *)(uintptr_t)(space->table[i] & ~0xFFFu));
    pmm_free_frame(space->table);
    pmm_free_frame(space->directory);
    space->directory = NULL;
    space->table = NULL;
}
