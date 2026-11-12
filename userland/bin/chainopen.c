#include <tinos/libc.h>

typedef unsigned (*chain_next_t)(void);

int main(void)
{
    int handle = tinos_dlopen("libtinoschain.so");
    chain_next_t next = (chain_next_t)tinos_dlsym(handle, "tinos_chain_next");
    if (handle <= 0 || !next || next() != 8 || next() != 9)
        return 1;
    const char message[] = "ring3: dlopen resolved a library dependency\n";
    tinos_write(1, message, sizeof(message) - 1);
    return 0;
}
