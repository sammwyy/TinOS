/* Port I/O primitives and small CPU helpers (x86). */
#ifndef TINOS_IO_H
#define TINOS_IO_H

#include <tinos/types.h>

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Short delay by writing to an unused diagnostic port. */
static inline void io_wait(void)
{
    outb(0x80, 0);
}

static inline void cli(void) { __asm__ volatile("cli"); }
static inline void sti(void) { __asm__ volatile("sti"); }
static inline void hlt(void) { __asm__ volatile("hlt"); }

static inline uint32_t eflags_read(void)
{
    uint32_t f;
    __asm__ volatile("pushfl; popl %0" : "=r"(f));
    return f;
}

/* Disable interrupts, returning the previous interrupt-enable state. */
static inline bool irq_save(void)
{
    bool enabled = (eflags_read() & 0x200) != 0;
    cli();
    return enabled;
}

static inline void irq_restore(bool enabled)
{
    if (enabled)
        sti();
}

#endif /* TINOS_IO_H */
