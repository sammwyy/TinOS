#include <tinos/libc.h>

int main(void)
{
    static const char message[] = "ring3: spinner started\n";
    tinos_write(1, message, sizeof(message) - 1);
    for (;;)
        __asm__ volatile("pause" : : : "memory");
}
