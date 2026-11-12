#include <tinos/libc.h>

typedef unsigned (*next_fn)(void);
typedef unsigned *(*pointer_fn)(void);

static int probe(void)
{
    int handle = tinos_dlopen("libtinosgd.so");
    next_fn next = (next_fn)tinos_dlsym(handle, "tinos_gd_next");
    next_fn zero = (next_fn)tinos_dlsym(handle, "tinos_gd_zero_next");
    pointer_fn pointer = (pointer_fn)tinos_dlsym(handle, "tinos_gd_pointer");
    unsigned *variable = tinos_dlsym(handle, "tinos_gd_counter");
    if (handle < 1 || !next || !zero || !pointer || !variable ||
        pointer() != variable || *variable != 3 || next() != 4 ||
        *variable != 4 || zero() != 1 || tinos_dlclose(handle) != 0)
        return -1;
    return 0;
}

int main(void)
{
    if (probe() < 0 || probe() < 0)
        return 1;
    const char message[] = "ring3: global-dynamic TLS keeps private state\n";
    tinos_write(1, message, sizeof(message) - 1);
    return 0;
}
