/* CPUID helpers plus machine reset / power-off. */

#include <tinos/arch.h>
#include <tinos/io.h>
#include <tinos/string.h>

static inline void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c,
                         uint32_t *d)
{
    __asm__ volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(0));
}

void cpu_vendor_string(char *out, size_t size)
{
    uint32_t a, b, c, d;
    char     buf[13];

    cpuid(0, &a, &b, &c, &d);
    memcpy(buf + 0, &b, 4);
    memcpy(buf + 4, &d, 4);
    memcpy(buf + 8, &c, 4);
    buf[12] = '\0';
    strlcpy(out, buf, size);
}

void cpu_brand_string(char *out, size_t size)
{
    uint32_t a, b, c, d;
    char     buf[49];

    cpuid(0x80000000u, &a, &b, &c, &d);
    if (a < 0x80000004u) {
        cpu_vendor_string(out, size);
        return;
    }
    for (int i = 0; i < 3; i++) {
        cpuid(0x80000002u + (uint32_t)i, &a, &b, &c, &d);
        memcpy(buf + i * 16 + 0, &a, 4);
        memcpy(buf + i * 16 + 4, &b, 4);
        memcpy(buf + i * 16 + 8, &c, 4);
        memcpy(buf + i * 16 + 12, &d, 4);
    }
    buf[48] = '\0';

    /* Intel pads the brand string with leading spaces. */
    const char *p = buf;
    while (*p == ' ')
        p++;
    strlcpy(out, p, size);
}

NORETURN void arch_halt(void)
{
    for (;;)
        __asm__ volatile("cli; hlt");
}

NORETURN void arch_reboot(void)
{
    /* Pulse the keyboard controller reset line, then fall back to a triple
     * fault by loading a null IDT and issuing an interrupt. */
    uint8_t status;
    do {
        status = inb(0x64);
        if (status & 0x01)
            (void)inb(0x60);
    } while (status & 0x02);
    outb(0x64, 0xFE);

    for (volatile int i = 0; i < 1000000; i++)
        ;

    struct PACKED {
        uint16_t limit;
        uint32_t base;
    } null_idt = { 0, 0 };
    __asm__ volatile("lidt %0; int $0x03" : : "m"(null_idt));
    arch_halt();
}

NORETURN void arch_shutdown(void)
{
    /* ACPI shutdown ports understood by QEMU (and Bochs). */
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    arch_halt();
}
