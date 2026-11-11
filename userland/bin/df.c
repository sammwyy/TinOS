#include <tinos/syscall.h>

void _start(void)
{
    static const char message[] = "ring3: direction flag isolated\n";
    unsigned number = TINOS_SYS_WRITE;
    __asm__ volatile("std; int $0x80; cld" : "+a"(number)
                     : "b"(1), "c"(message), "d"(sizeof(message) - 1)
                     : "memory");
    __asm__ volatile("int $0x80" : : "a"(TINOS_SYS_EXIT), "b"(0)
                     : "memory");
    for (;;)
        ;
}
