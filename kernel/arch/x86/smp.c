/* SMP application-processor bring-up - see tinos/smp.h for the scope of
 * what this does (and deliberately does not yet do). */

#include <tinos/acpi.h>
#include <tinos/arch.h>
#include <tinos/heap.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/lapic.h>
#include <tinos/mm.h>
#include <tinos/smp.h>
#include <tinos/string.h>
#include <tinos/timer.h>

#define AP_STACK_SIZE (16u * 1024u)

extern uint8_t ap_trampoline_start[];
extern uint8_t ap_trampoline_end[];
extern uint8_t ap_gdt_ptr_copy[6]; /* 6-byte GDTR image, inside the blob */

/* Both read by trampoline.S, in 32-bit protected mode, right before it
 * calls ap_entry(). APs are brought up one at a time (see the loop below),
 * so there is never more than one core reading ap_stack_top at once. */
uint32_t ap_stack_top;

static volatile uint32_t cpus_online = 1; /* the boot processor counts itself */
static uint32_t lapic_phys_base;

/* Entered by trampoline.S once an AP has reached 32-bit protected mode
 * with paging still off and only the shared GDT loaded. Never returns. */
void ap_entry(void)
{
    paging_activate();
    if (!arch_fpu_init())
        panic("SMP: apic core lacks FXSAVE/SSE2");
    idt_load_shared();
    lapic_init(lapic_phys_base);

    uint32_t id = lapic_id();
    __atomic_fetch_add(&cpus_online, 1, __ATOMIC_SEQ_CST);
    klog("[smp ] core apic_id=%u online (%u total)\n", id, cpus_online);

    /* Parked: see tinos/smp.h for why nothing further happens here yet. */
    for (;;)
        __asm__ volatile("cli; hlt");
}

size_t smp_init(const mb_info_t *mbi)
{
    acpi_madt_info_t madt;
    if (!acpi_find_madt(mbi, &madt)) {
        klog("[smp ] no ACPI MADT found, staying single-core\n");
        return cpus_online;
    }
    if (madt.cpu_count <= 1) {
        klog("[smp ] MADT reports a single CPU\n");
        return cpus_online;
    }

    lapic_phys_base = madt.lapic_address;
    lapic_init(lapic_phys_base);
    uint32_t bsp_id = lapic_id();

    memcpy(ap_gdt_ptr_copy, gdt_pointer(), sizeof(ap_gdt_ptr_copy));
    uint8_t vector = (uint8_t)((uintptr_t)ap_trampoline_start >> 12);

    klog("[smp ] MADT reports %u CPUs (boot processor apic_id=%u)\n",
        (uint32_t)madt.cpu_count, bsp_id);

    for (size_t i = 0; i < madt.cpu_count; i++) {
        uint32_t id = madt.apic_ids[i];
        if (id == bsp_id)
            continue;

        uint8_t *stack = (uint8_t *)kmalloc(AP_STACK_SIZE);
        if (!stack) {
            klog("[smp ] out of memory bringing up apic_id=%u\n", id);
            break;
        }
        ap_stack_top =
            (uint32_t)(uintptr_t)(stack + AP_STACK_SIZE) & ~0xFu;

        uint32_t before = cpus_online;

        /* Universal MP-startup sequence (Intel MP spec / SDM Vol 3 8.4.4):
         * INIT, wait, SIPI, short wait, SIPI again. Real hardware only
         * needs the first SIPI; sending a second is the documented,
         * harmless way to cover chipsets that drop the first one. */
        lapic_send_init(id);
        timer_sleep(10);
        lapic_send_sipi(id, vector);
        timer_sleep(1);
        lapic_send_sipi(id, vector);

        uint64_t deadline = timer_ticks() + 200;
        while (cpus_online == before && timer_ticks() < deadline)
            __asm__ volatile("pause");

        if (cpus_online == before)
            klog("[smp ] WARNING: apic_id=%u did not respond\n", id);
    }

    return cpus_online;
}
