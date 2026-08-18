/* SMP application-processor bring-up.
 *
 * smp_init() finds every usable CPU via ACPI's MADT and starts each one
 * (besides the boot processor) through kernel/arch/x86/trampoline.S. Once
 * an AP reports in, it parks in a `cli; hlt` loop: this only proves the
 * hardware bring-up path (paging, SSE, the shared GDT/IDT, the local
 * APIC) works. Handing the scheduler real work on these cores is future
 * work - every shared kernel structure (the heap, the thread table, the
 * filesystem tree, the compositor) is currently protected only by
 * irq_save()/irq_restore(), which stops this core's own interrupts but
 * not a second core running at the same time, and would need real
 * spinlocks first.
 */
#ifndef TINOS_SMP_H
#define TINOS_SMP_H

#include <tinos/multiboot2.h>
#include <tinos/types.h>

/* Returns the number of CPUs now online (always >= 1: the boot processor
 * counts itself even if no other core could be started). */
size_t smp_init(const mb_info_t *mbi);

/* The boot processor's local APIC ID, valid once smp_init() has run (it
 * always brings up the local APIC, even single-core / without ACPI). Used
 * by kernel/core/thread.c to keep filesystem/network/gfx-touching threads
 * pinned to the one core whose access to that state is actually safe. */
uint32_t smp_bsp_apic_id(void);

#endif /* TINOS_SMP_H */
