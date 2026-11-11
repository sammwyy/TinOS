/* System information provider used by the shell and the desktop. */

#include <tinos/arch.h>
#include <tinos/gfx.h>
#include <tinos/heap.h>
#include <tinos/mm.h>
#include <tinos/printf.h>
#include <tinos/sh.h>
#include <tinos/string.h>

void sys_info_get(sys_info_t *out)
{
    memset(out, 0, sizeof(*out));

    out->uptime_ms = (uint32_t)timer_ticks();

    rtc_time_t now;
    rtc_read(&now);
    if (now.valid)
        ksnprintf(out->datetime, sizeof(out->datetime),
                  "%04u-%02u-%02u %02u:%02u:%02u", now.year, now.month,
                  now.day, now.hour, now.minute, now.second);
    else
        ksnprintf(out->datetime, sizeof(out->datetime),
                  "clock unavailable (up %u s)", out->uptime_ms / 1000);

    cpu_brand_string(out->cpu, sizeof(out->cpu));
    strlcpy(out->platform, "i386 protected mode (TinOS BIOS loader)",
            sizeof(out->platform));

    out->mem_total = pmm_total_bytes();
    out->mem_free  = pmm_free_bytes();

    heap_stats_t heap;
    heap_get_stats(&heap);
    out->heap_total = heap.heap_total;
    out->heap_used  = heap.heap_used;

    out->screen_width  = gfx_width();
    out->screen_height = gfx_height();
}
