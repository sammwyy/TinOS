/* Glue between the portable filesystem code and the architecture timer. */

#include <tinos/arch.h>
#include <tinos/fs.h>

uint32_t fs_clock_ms(void)
{
    return (uint32_t)timer_ticks();
}
