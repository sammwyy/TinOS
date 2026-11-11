#include <tinos/syscall.h>

static int write_console(const char *buffer, unsigned length)
{
    unsigned result;
    __asm__ volatile("int $0x80" : "=a"(result)
                     : "a"(TINOS_SYS_WRITE), "b"(1), "c"(buffer),
                       "d"(length) : "memory");
    return (int)result;
}

__attribute__((noreturn)) static void exit_process(int status)
{
    __asm__ volatile("int $0x80" : : "a"(TINOS_SYS_EXIT), "b"(status)
                     : "memory");
    for (;;)
        ;
}

void _start(void)
{
    static const char message[] = "ring3: hello from an isolated ELF\n";
    write_console(message, sizeof(message) - 1);
    exit_process(17);
}
