#include <tinos/syscall.h>

void _start(void)
{
    *(volatile unsigned *)0x00100000 = 0;
    __asm__ volatile("int $0x80" : : "a"(TINOS_SYS_EXIT), "b"(0)
                     : "memory");
    for (;;)
        ;
}
