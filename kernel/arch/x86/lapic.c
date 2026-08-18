/* Local APIC register access. The LAPIC's MMIO page lives well above 1 MiB
 * (0xFEE00000 on every machine QEMU or real firmware hands us) but that
 * address is covered by the identity map like everything else, so it is
 * used directly as a pointer. */

#include <tinos/lapic.h>
#include <tinos/kernel.h>

#define LAPIC_REG_ID     0x020
#define LAPIC_REG_EOI    0x0B0
#define LAPIC_REG_SVR    0x0F0
#define LAPIC_REG_ICR_LO 0x300
#define LAPIC_REG_ICR_HI 0x310

#define LAPIC_SVR_ENABLE (1u << 8)
#define ICR_DELIVERY_PENDING (1u << 12)
#define ICR_INIT     0x00004500u /* delivery=INIT, level assert, edge */
#define ICR_STARTUP  0x00004600u /* delivery=Startup, edge */

static volatile uint32_t *lapic;

static inline uint32_t lapic_read(uint32_t reg)
{
    return lapic[reg / 4];
}

static inline void lapic_write(uint32_t reg, uint32_t value)
{
    lapic[reg / 4] = value;
}

void lapic_init(uint32_t phys_base)
{
    lapic = (volatile uint32_t *)(uintptr_t)phys_base;
    /* Spurious vector 0xFF: any value works since TinOS never expects one,
     * it just has to sit above the 32 CPU exception vectors. */
    lapic_write(LAPIC_REG_SVR, lapic_read(LAPIC_REG_SVR) |
                               LAPIC_SVR_ENABLE | 0xFF);
}

uint32_t lapic_id(void)
{
    return lapic_read(LAPIC_REG_ID) >> 24;
}

void lapic_eoi(void)
{
    lapic_write(LAPIC_REG_EOI, 0);
}

static void icr_wait_idle(void)
{
    while (lapic_read(LAPIC_REG_ICR_LO) & ICR_DELIVERY_PENDING)
        ;
}

void lapic_send_init(uint32_t apic_id)
{
    lapic_write(LAPIC_REG_ICR_HI, apic_id << 24);
    lapic_write(LAPIC_REG_ICR_LO, ICR_INIT);
    icr_wait_idle();
}

void lapic_send_sipi(uint32_t apic_id, uint8_t vector)
{
    lapic_write(LAPIC_REG_ICR_HI, apic_id << 24);
    lapic_write(LAPIC_REG_ICR_LO, ICR_STARTUP | vector);
    icr_wait_idle();
}
