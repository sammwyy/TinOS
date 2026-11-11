#include <tinos/syscall.h>

void _start(void)
{
    unsigned char value;
    __asm__ volatile("inb $0x60, %0" : "=a"(value));
    __asm__ volatile("int $0x80" : : "a"(TINOS_SYS_EXIT), "b"(value)
                     : "memory");
    for (;;)
        ;
}
