#include <tinos/libc.h>

typedef unsigned (*next_fn)(void);

int main(void)
{
    int chain = tinos_dlopen("libtinoschain.so");
    int second = tinos_dlopen("libtinoschain.so");
    int state = tinos_dlopen("libtinostate.so");
    next_fn next = (next_fn)tinos_dlsym(chain, "tinos_chain_next");
    tinos_system_info_t before, after;
    if (tinos_sysinfo(&before) < 0)
        return 1;
    if (chain <= 0 || second != chain || state <= 0 || !next ||
        next() != 9 || tinos_dlclose(chain) != 0 ||
        next() != 10 || tinos_dlclose(state) != 0 ||
        tinos_dlsym(state, "tinos_state_pointer") ||
        next() != 11 || tinos_dlclose(second) != 0 ||
        tinos_dlsym(chain, "tinos_chain_next") ||
        tinos_dlclose(chain) != -1)
        return 1;
    if (tinos_sysinfo(&after) < 0 ||
        after.memory_free_kib < before.memory_free_kib + 8)
        return 1;

    chain = tinos_dlopen("libtinoschain.so");
    next = (next_fn)tinos_dlsym(chain, "tinos_chain_next");
    if (chain <= 0 || !next || next() != 9 ||
        tinos_dlclose(chain) != 0)
        return 2;
    const char message[] = "ring3: dlclose released private library state\n";
    tinos_write(1, message, sizeof(message) - 1);
    return 0;
}
