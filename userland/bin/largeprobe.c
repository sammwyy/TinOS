#include <tinos/libc.h>

typedef unsigned (*next_fn)(void);

int main(void)
{
    int handle = tinos_dlopen("libtinoslarge.so");
    next_fn next = (next_fn)tinos_dlsym(handle, "tinos_large_next");
    if (handle < 1 || !next || next() != 10 || next() != 11 ||
        tinos_dlclose(handle) != 0)
        return 1;
    handle = tinos_dlopen("libtinoslarge.so");
    next = (next_fn)tinos_dlsym(handle, "tinos_large_next");
    if (handle < 1 || !next || next() != 10 ||
        tinos_dlclose(handle) != 0)
        return 2;
    const char message[] = "ring3: large library keeps private data\n";
    tinos_write(1, message, sizeof(message) - 1);
    return 0;
}
