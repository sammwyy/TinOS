__thread unsigned tinos_tls_counter = 5;
static __thread unsigned zero;

unsigned tinos_tls_next(void)
{
    return ++tinos_tls_counter;
}

unsigned *tinos_tls_pointer(void)
{
    return &tinos_tls_counter;
}

unsigned tinos_tls_zero_next(void)
{
    return ++zero;
}
