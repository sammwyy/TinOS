/* COM1 serial port - the kernel's always-available diagnostic channel.
 *
 * QEMU forwards this to stdout/a file, which is how the boot log and the
 * automated tests observe what the kernel is doing.
 */

#include <tinos/console.h>
#include <tinos/io.h>

#define COM1 0x3F8

static bool serial_ready;

void serial_init(void)
{
    outb(COM1 + 1, 0x00); /* disable interrupts        */
    outb(COM1 + 3, 0x80); /* enable divisor latch      */
    outb(COM1 + 0, 0x01); /* divisor 1 => 115200 baud  */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* 8 bits, no parity, 1 stop */
    outb(COM1 + 2, 0xC7); /* enable + clear FIFOs      */
    outb(COM1 + 4, 0x0B); /* RTS/DSR set               */
    serial_ready = true;
}

static bool tx_empty(void)
{
    return (inb(COM1 + 5) & 0x20) != 0;
}

void serial_putc(char c)
{
    if (!serial_ready)
        return;
    /* Bounded wait: a missing/blocked UART must never hang the kernel. */
    for (int i = 0; i < 100000 && !tx_empty(); i++)
        ;
    if (c == '\n')
        serial_putc('\r');
    outb(COM1, (uint8_t)c);
}

void serial_write(const char *s)
{
    while (*s)
        serial_putc(*s++);
}
