/* CMOS real time clock.
 *
 * Provides the wall-clock time shown by the taskbar and the `date` command.
 * Readings are taken while the update-in-progress flag is clear and are
 * repeated until two consecutive reads agree, which avoids the classic
 * rollover race.
 */

#include <tinos/arch.h>
#include <tinos/io.h>
#include <tinos/string.h>

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static bool bcd_mode;
static bool hour24;

static uint8_t cmos_read(uint8_t reg)
{
    outb(CMOS_ADDR, (uint8_t)(reg | 0x80)); /* keep NMI disabled */
    io_wait();
    return inb(CMOS_DATA);
}

static bool rtc_updating(void)
{
    return (cmos_read(0x0A) & 0x80) != 0;
}

static uint8_t bcd_to_bin(uint8_t v)
{
    return (uint8_t)((v & 0x0F) + ((v >> 4) * 10));
}

void rtc_init(void)
{
    uint8_t status_b = cmos_read(0x0B);
    bcd_mode = (status_b & 0x04) == 0;
    hour24   = (status_b & 0x02) != 0;
}

static void rtc_read_raw(rtc_time_t *t)
{
    t->second = cmos_read(0x00);
    t->minute = cmos_read(0x02);
    t->hour   = cmos_read(0x04);
    t->day    = cmos_read(0x07);
    t->month  = cmos_read(0x08);
    t->year   = cmos_read(0x09);
}

void rtc_read(rtc_time_t *out)
{
    rtc_time_t a, b;
    int        guard = 0;

    memset(out, 0, sizeof(*out));

    while (rtc_updating() && ++guard < 1000000)
        ;

    rtc_read_raw(&a);
    do {
        b = a;
        while (rtc_updating() && ++guard < 2000000)
            ;
        rtc_read_raw(&a);
    } while ((a.second != b.second || a.minute != b.minute ||
              a.hour != b.hour || a.day != b.day || a.month != b.month ||
              a.year != b.year) &&
             ++guard < 2000000);

    bool pm = (a.hour & 0x80) != 0;
    a.hour &= 0x7F;

    if (bcd_mode) {
        a.second = bcd_to_bin(a.second);
        a.minute = bcd_to_bin(a.minute);
        a.hour   = bcd_to_bin((uint8_t)a.hour);
        a.day    = bcd_to_bin(a.day);
        a.month  = bcd_to_bin(a.month);
        a.year   = bcd_to_bin((uint8_t)a.year);
    }

    if (!hour24) {
        if (pm && a.hour < 12)
            a.hour = (uint8_t)(a.hour + 12);
        else if (!pm && a.hour == 12)
            a.hour = 0;
    }

    a.year = (uint16_t)(2000 + a.year);
    a.valid = a.month >= 1 && a.month <= 12 && a.day >= 1 && a.day <= 31 &&
              a.hour < 24 && a.minute < 60 && a.second < 60;
    *out = a;
}
