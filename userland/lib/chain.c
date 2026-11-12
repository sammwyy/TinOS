extern unsigned tinos_state_next(void);
extern unsigned tinos_state_shared_symbol;

unsigned tinos_chain_next(void)
{
    return tinos_state_next();
}

unsigned tinos_chain_object_next(void)
{
    return ++tinos_state_shared_symbol;
}
