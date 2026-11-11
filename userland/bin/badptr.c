#include <tinos/syscall.h>

void _start(void)
{
    unsigned result;
    __asm__ volatile("int $0x80" : "=a"(result)
                     : "a"(TINOS_SYS_WRITE), "b"(1), "c"(0x00100000),
                       "d"(4) : "memory");
    static const char message[] = "ring3: bad pointer rejected\n";
    if (result == (unsigned)-1) {
        __asm__ volatile("int $0x80" : : "a"(TINOS_SYS_WRITE), "b"(1),
                         "c"(message), "d"(sizeof(message) - 1) : "memory");
    }
    __asm__ volatile("int $0x80" : : "a"(TINOS_SYS_EXIT),
                     "b"(result == (unsigned)-1 ? 0 : 1) : "memory");
    for (;;)
        ;
}
