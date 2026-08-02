/* Kernel logging.
 *
 * Driver level messages go to the serial port only, so the text-mode boot
 * screen stays readable; the boot sequence in main.c decides what the user
 * sees on the console. */

#include <tinos/console.h>
#include <tinos/kernel.h>
#include <tinos/printf.h>

void klog(const char *fmt, ...)
{
    char    buf[512];
    va_list ap;

    va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    serial_write(buf);
}
