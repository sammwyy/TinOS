#include <tinos/syscall.h>

void _start(void)
{
    __asm__ volatile("fldz");
    __asm__ volatile("int $0x80" : : "a"(TINOS_SYS_EXIT), "b"(0)
                     : "memory");
    for (;;)
        ;
}
