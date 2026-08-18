/* Kernel logging.
 *
 * Driver level messages go to the serial port only, so the text-mode boot
 * screen stays readable; the boot sequence in main.c decides what the user
 * sees on the console. */

#include <tinos/console.h>
#include <tinos/kernel.h>
#include <tinos/printf.h>

/* klog() itself is variadic and so cannot be forwarded through a plain
 * function pointer; native apps format locally (they link printf.c
 * directly) and reach the serial port through this instead. */
void klog_str(const char *text)
{
    serial_write(text);
}

void klog(const char *fmt, ...)
{
    char    buf[512];
    va_list ap;

    va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    klog_str(buf);
}
