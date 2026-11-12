#include <tinos/libc.h>

extern unsigned tinos_chain_next(void);
extern unsigned tinos_chain_object_next(void);

typedef unsigned *(*state_pointer_t)(void);

int main(void)
{
    int handle = tinos_dlopen("libtinostate.so");
    state_pointer_t state = (state_pointer_t)tinos_dlsym(
        handle, "tinos_state_pointer");
    unsigned *object = tinos_dlsym(handle, "tinos_state_shared_symbol");
    if (handle <= 0 || !state || *state() != 7 ||
        tinos_chain_next() != 8 || *state() != 8 ||
        tinos_chain_next() != 9 || *state() != 9 ||
        !object || *object != 30 ||
        tinos_chain_object_next() != 31 || *object != 31)
        return 1;
    const char message[] = "ring3: chained libraries keep private state\n";
    tinos_write(1, message, sizeof(message) - 1);
    return 0;
}
