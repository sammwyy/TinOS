#include <tinos/syscall.h>

static void write_console(const char *data, unsigned length)
{
    unsigned number = TINOS_SYS_WRITE;
    __asm__ volatile("int $0x80" : "+a"(number) : "b"(1),
                     "c"(data), "d"(length) : "memory");
}

void _start(void)
{
    static const char prompt[] = "ring3: type a line> ";
    static const char prefix[] = "ring3 echo: ";
    char line[64];
    unsigned length = 0;
    write_console(prompt, sizeof(prompt) - 1);
    while (length < sizeof(line) - 1) {
        char ch;
        unsigned result;
        __asm__ volatile("int $0x80" : "=a"(result)
                         : "a"(TINOS_SYS_READ), "b"(0), "c"(&ch), "d"(1)
                         : "memory");
        if (result != 1)
            break;
        write_console(&ch, 1);
        if (ch == '\n')
            break;
        line[length++] = ch;
    }
    write_console(prefix, sizeof(prefix) - 1);
    write_console(line, length);
    write_console("\n", 1);
    __asm__ volatile("int $0x80" : : "a"(TINOS_SYS_EXIT), "b"(0)
                     : "memory");
    for (;;)
        ;
}
