__thread unsigned tinos_gd_counter = 3;
__thread unsigned tinos_gd_zero;

unsigned tinos_gd_next(void)
{
    return ++tinos_gd_counter;
}

unsigned *tinos_gd_pointer(void)
{
    return &tinos_gd_counter;
}

unsigned tinos_gd_zero_next(void)
{
    return ++tinos_gd_zero;
}
