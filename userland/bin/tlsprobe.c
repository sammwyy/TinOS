#include <tinos/libc.h>

typedef unsigned (*next_fn)(void);
typedef unsigned *(*pointer_fn)(void);

static int wait_for_pit(void)
{
    tinos_system_info_t start, now;
    if (tinos_sysinfo(&start) < 0)
        return -1;
    do {
        volatile unsigned work = 0;
        for (unsigned i = 0; i < 1024; i++)
            work += i;
        if (tinos_sysinfo(&now) < 0)
            return -1;
    } while (now.uptime_ms - start.uptime_ms < 20);
    return 0;
}

static int probe(void)
{
    int handle = tinos_dlopen("libtinostls.so");
    next_fn next = (next_fn)tinos_dlsym(handle, "tinos_tls_next");
    next_fn zero = (next_fn)tinos_dlsym(handle, "tinos_tls_zero_next");
    pointer_fn pointer = (pointer_fn)tinos_dlsym(handle, "tinos_tls_pointer");
    unsigned *variable = tinos_dlsym(handle, "tinos_tls_counter");
    if (handle < 1 || !next || !zero || !pointer || !variable ||
        pointer() != variable || *variable != 5 ||
        next() != 6 || wait_for_pit() < 0 || *pointer() != 6 ||
        next() != 7 || zero() != 1 ||
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
