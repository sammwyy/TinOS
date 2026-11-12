static unsigned counter = 5;
static unsigned * volatile active_counter = &counter;
unsigned tinos_state_shared_symbol = 30;

__attribute__((constructor)) static void initialize_state(void)
{
    *active_counter = 7;
}

__attribute__((destructor)) static void finalize_state(void)
{
    tinos_state_shared_symbol = 77;
}

unsigned tinos_state_next(void)
{
    return ++*active_counter;
}

unsigned *tinos_state_pointer(void)
{
    return &counter;
}
