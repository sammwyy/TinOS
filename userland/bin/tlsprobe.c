#include <tinos/libc.h>

typedef unsigned (*next_fn)(void);
typedef unsigned *(*pointer_fn)(void);

static int probe(void)
{
    int handle = tinos_dlopen("libtinostls.so");
    next_fn next = (next_fn)tinos_dlsym(handle, "tinos_tls_next");
    next_fn zero = (next_fn)tinos_dlsym(handle, "tinos_tls_zero_next");
    pointer_fn pointer = (pointer_fn)tinos_dlsym(handle, "tinos_tls_pointer");
    unsigned *variable = tinos_dlsym(handle, "tinos_tls_counter");
    if (handle < 1 || !next || !zero || !pointer || !variable ||
        pointer() != variable || *variable != 5 ||
        next() != 6 || *pointer() != 6 || zero() != 1 ||
        tinos_dlclose(handle) != 0)
        return -1;
    return 0;
}

int main(void)
{
    if (probe() < 0 || probe() < 0)
        return 1;
    const char message[] = "ring3: private initial-exec TLS survives preemption\n";
    tinos_write(1, message, sizeof(message) - 1);
    return 0;
}
