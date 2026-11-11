#include <tinos/libc.h>
#include <tinos/syscall.h>

static int invoke(unsigned number, unsigned a, unsigned b, unsigned c)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result)
                     : "a"(number), "b"(a), "c"(b), "d"(c) : "memory");
    return result;
}

int tinos_open(const char *path)
{
    unsigned length = 0;
    while (length < 511 && path[length])
        length++;
    if (!length || length == 511)
        return -1;
    return invoke(TINOS_SYS_OPEN, (unsigned)path, length, 0);
}

int tinos_close(int fd)
{
    return invoke(TINOS_SYS_CLOSE, (unsigned)fd, 0, 0);
}

int tinos_read(int fd, void *buffer, unsigned length)
{
    return invoke(TINOS_SYS_READ, (unsigned)fd, (unsigned)buffer, length);
}

int tinos_write(int fd, const void *buffer, unsigned length)
{
    return invoke(TINOS_SYS_WRITE, (unsigned)fd, (unsigned)buffer, length);
}

__attribute__((noreturn)) void tinos_exit(int status)
{
    invoke(TINOS_SYS_EXIT, (unsigned)status, 0, 0);
    for (;;)
        ;
}
