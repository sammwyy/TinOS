#include <tinos/libc.h>
#include <tinos/syscall.h>

static int invoke(unsigned number, unsigned a, unsigned b, unsigned c)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result)
                     : "a"(number), "b"(a), "c"(b), "d"(c) : "memory");
    return result;
}

static int invoke4(unsigned number, unsigned a, unsigned b, unsigned c,
                   unsigned d)
{
    int result;
    __asm__ volatile("int $0x80" : "=a"(result)
                     : "a"(number), "b"(a), "c"(b), "d"(c), "S"(d)
                     : "memory");
    return result;
}

static unsigned path_length(const char *path)
{
    if (!path)
        return 0;
    unsigned length = 0;
    while (length < 512 && path[length])
        length++;
    return length == 512 ? 0 : length;
}

int tinos_open(const char *path)
{
    unsigned length = path_length(path);
    if (!length)
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

int tinos_spawn(const char *path, unsigned argc, const char *const argv[])
{
    unsigned length = path_length(path);
    if (!length)
        return -1;
    return invoke4(TINOS_SYS_SPAWN, (unsigned)path, length, argc,
                   (unsigned)argv);
}

int tinos_wait(int pid, int *status)
{
    return invoke(TINOS_SYS_WAIT, (unsigned)pid, (unsigned)status, 0);
}

int tinos_getpid(void)
{
    return invoke(TINOS_SYS_GETPID, 0, 0, 0);
}

__attribute__((noreturn)) void tinos_exit(int status)
{
    invoke(TINOS_SYS_EXIT, (unsigned)status, 0, 0);
    for (;;)
        ;
}
