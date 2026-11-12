#include <tinos/libc.h>

typedef unsigned (*chain_next_t)(void);
typedef unsigned (*chain_initial_t)(void);

int main(void)
{
    int handle = tinos_dlopen("libtinoschain.so");
    chain_next_t next = (chain_next_t)tinos_dlsym(handle, "tinos_chain_next");
    chain_initial_t initialized = (chain_initial_t)tinos_dlsym(
        handle, "tinos_chain_initial_value");
    if (handle <= 0 || !next || !initialized || initialized() != 8 ||
        next() != 9 || next() != 10)
        return 1;
    const char message[] = "ring3: dlopen resolved a library dependency\n";
    tinos_write(1, message, sizeof(message) - 1);
    return 0;
}
