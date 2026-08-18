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
#include <tinos/thread.h>
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
static uint32_t bsp_apic_id;

uint32_t smp_bsp_apic_id(void)
{
    return bsp_apic_id;
}

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
    klog("[smp ] cpu apic=%u up (%u online)\n", id, cpus_online);

    /* No local timer is armed for this core, so there is nothing to wake
     * a halted core back up - spin cooperatively instead of hlt. A real
     * per-core preemption timer (the local APIC has one) was tried and
     * pulled back out: it exposed a genuine cross-core race in how
     * thread_preempt()/thread_run() hand a thread off between cores that
     * was not fully run to ground in the time available, and shipping an
     * intermittent scheduler crash is worse than not having AP-side
     * preemption at all. claim_ready_thread() and thread_preempt() are
     * still both correctly pool_lock-protected for the cooperative case
     * this loop actually exercises. Threads pinned to the boot processor
     * (filesystem/network/gfx work) are simply never handed to this loop;
     * see thread_spawn_pinned(). */
    for (;;) {
        if (thread_run(4) == 0)
            __asm__ volatile("pause");
    }
}

size_t smp_init(const mb_info_t *mbi)
{
    acpi_madt_info_t madt;
    bool has_madt = acpi_find_madt(mbi, &madt);

    /* Bring up this core's own local APIC either way: even single-core,
     * thread.c needs lapic_id() to answer "which CPU am I". 0xFEE00000 is
     * the architectural default on every machine that has one at all. */
    lapic_phys_base = has_madt ? madt.lapic_address : 0xFEE00000u;
    lapic_init(lapic_phys_base);
    bsp_apic_id = lapic_id();

    if (!has_madt) {
        klog("[smp ] no MADT, single-core (apic=%u)\n", bsp_apic_id);
        return cpus_online;
    }
    if (madt.cpu_count <= 1) {
        klog("[smp ] MADT: 1 CPU (apic=%u)\n", bsp_apic_id);
        return cpus_online;
    }

    uint32_t bsp_id = bsp_apic_id;
    memcpy(ap_gdt_ptr_copy, gdt_pointer(), sizeof(ap_gdt_ptr_copy));
    uint8_t vector = (uint8_t)((uintptr_t)ap_trampoline_start >> 12);

    klog("[smp ] MADT: %u CPUs (bsp apic=%u)\n",
        (uint32_t)madt.cpu_count, bsp_id);

    for (size_t i = 0; i < madt.cpu_count; i++) {
        uint32_t id = madt.apic_ids[i];
        if (id == bsp_id)
            continue;

        uint8_t *stack = (uint8_t *)kmalloc(AP_STACK_SIZE);
        if (!stack) {
            klog("[smp ] OOM bringing up apic=%u\n", id);
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
            klog("[smp ] apic=%u did not respond\n", id);
    }

    return cpus_online;
}
