extern unsigned tinos_state_next(void);
extern unsigned tinos_state_shared_symbol;

static unsigned chain_initial_value;

__attribute__((constructor)) static void initialize_chain(void)
{
    chain_initial_value = tinos_state_next();
}

unsigned tinos_chain_initial_value(void)
{
    return chain_initial_value;
}

unsigned tinos_chain_next(void)
{
    return tinos_state_next();
}

unsigned tinos_chain_object_next(void)
{
    return ++tinos_state_shared_symbol;
}
