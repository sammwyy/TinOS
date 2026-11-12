static unsigned counter = 7;
static unsigned * volatile active_counter = &counter;
unsigned tinos_state_shared_symbol = 30;

unsigned tinos_state_next(void)
{
    return ++*active_counter;
}

unsigned *tinos_state_pointer(void)
{
    return &counter;
}
