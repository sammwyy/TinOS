/* 8253/8254 programmable interval timer, used as the system tick.
 *
 * The tick rate is 1 kHz so timer_ticks() is simply milliseconds of uptime,
 * which the taskbar clock, the cursor blink and timer_sleep() all build on.
 */

#include <tinos/arch.h>
#include <tinos/io.h>

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_HZ  1193182u

static volatile uint64_t ticks;
static uint32_t          tick_hz = TIMER_HZ;

static void timer_isr(regs_t *regs)
{
    UNUSED(regs);
    ticks++;
}

void timer_init(uint32_t frequency)
{
    if (frequency == 0)
        frequency = TIMER_HZ;
    tick_hz = frequency;

    uint32_t divisor = PIT_BASE_HZ / frequency;
    if (divisor == 0)
        divisor = 1;
    if (divisor > 0xFFFF)
        divisor = 0xFFFF;

    /* channel 0, lobyte/hibyte, mode 3 (square wave), binary */
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    ticks = 0;
    irq_install(0, timer_isr);
}

uint64_t timer_ticks(void)
{
    /* 64-bit read on a 32-bit machine: retry if the low half wrapped. */
    uint64_t value;
    do {
        value = ticks;
    } while (value != ticks);
    return value;
}

uint32_t timer_seconds(void)
{
    return (uint32_t)(timer_ticks() / tick_hz);
}

void timer_sleep(uint32_t ms)
{
    uint64_t target = timer_ticks() + ms;
    while (timer_ticks() < target)
        __asm__ volatile("sti; hlt");
}
