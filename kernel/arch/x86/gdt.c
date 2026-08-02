/* Flat 32-bit global descriptor table.
 *
 * GRUB leaves us with a GDT of its own that we must not rely on, so the
 * kernel installs a minimal flat model: one code and one data segment
 * spanning the whole 4 GiB address space.  TinOS runs entirely in ring 0
 * and does not use paging, so link addresses are physical addresses.
 */

#include <tinos/arch.h>
#include <tinos/kernel.h>
#include <tinos/string.h>

typedef struct PACKED {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity; /* limit 19:16 in the low nibble */
    uint8_t  base_high;
} gdt_entry_t;

typedef struct PACKED {
    uint16_t limit;
    uint32_t base;
} gdt_ptr_t;

static gdt_entry_t gdt[3];
static gdt_ptr_t   gdt_ptr;

static void gdt_set(int index, uint32_t base, uint32_t limit, uint8_t access,
                    uint8_t flags)
{
    gdt[index].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt[index].base_low    = (uint16_t)(base & 0xFFFF);
    gdt[index].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    gdt[index].access      = access;
    gdt[index].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (flags & 0xF0));
    gdt[index].base_high   = (uint8_t)((base >> 24) & 0xFF);
}

void gdt_init(void)
{
    memset(gdt, 0, sizeof(gdt));

    /* 0x00 null, 0x08 ring-0 code (RX), 0x10 ring-0 data (RW).
     * access: present | ring0 | code-or-data | exec | rw
     * flags : granularity 4 KiB | 32-bit */
    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0x000FFFFF, 0x9A, 0xC0);
    gdt_set(2, 0, 0x000FFFFF, 0x92, 0xC0);

    gdt_ptr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdt_ptr.base  = (uint32_t)(uintptr_t)&gdt;

    __asm__ volatile("lgdt %0\n\t"
                     "movw %1, %%ax\n\t"
                     "movw %%ax, %%ds\n\t"
                     "movw %%ax, %%es\n\t"
                     "movw %%ax, %%fs\n\t"
                     "movw %%ax, %%gs\n\t"
                     "movw %%ax, %%ss\n\t"
                     "ljmp %2, $1f\n"
                     "1:\n"
                     :
                     : "m"(gdt_ptr), "i"(GDT_KERNEL_DATA), "i"(GDT_KERNEL_CODE)
                     : "eax", "memory");
}
