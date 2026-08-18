/* Local APIC: per-CPU interrupt controller, and the only way to send the
 * INIT/SIPI IPIs that start an SMP application processor. */
#ifndef TINOS_LAPIC_H
#define TINOS_LAPIC_H

#include <tinos/types.h>

/* Software-enables the calling core's local APIC. Every core (the boot
 * processor included) must call this on itself - it is not shared state. */
void lapic_init(uint32_t phys_base);

uint32_t lapic_id(void);
void     lapic_eoi(void);

/* IPI helpers used only by SMP bring-up (see kernel/arch/x86/smp.c). */
void lapic_send_init(uint32_t apic_id);
void lapic_send_sipi(uint32_t apic_id, uint8_t vector);

#endif /* TINOS_LAPIC_H */
