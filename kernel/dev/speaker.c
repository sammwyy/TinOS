#include <tinos/arch.h>
#include <tinos/io.h>
#include <tinos/speaker.h>

#define PIT_CHANNEL2 0x42
#define PIT_COMMAND  0x43
#define PIT_BASE_HZ  1193182u
#define SPEAKER_PORT 0x61

void speaker_beep(uint32_t frequency, uint32_t duration_ms)
{
    if (!frequency || !duration_ms)
        return;
    uint32_t divisor = PIT_BASE_HZ / frequency;
    if (divisor == 0)
        divisor = 1;
    if (divisor > 0xffff)
        divisor = 0xffff;

    uint8_t original = inb(SPEAKER_PORT);
    outb(PIT_COMMAND, 0xb6);
    outb(PIT_CHANNEL2, (uint8_t)divisor);
    outb(PIT_CHANNEL2, (uint8_t)(divisor >> 8));
    outb(SPEAKER_PORT, (uint8_t)(original | 0x03));
    timer_sleep(duration_ms);
    outb(SPEAKER_PORT, (uint8_t)(original & (uint8_t)~0x03));
}
