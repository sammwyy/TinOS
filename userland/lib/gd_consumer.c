extern __thread unsigned tinos_gd_counter;

unsigned tinos_gd_consumer_next(void)
{
    return ++tinos_gd_counter;
}

unsigned *tinos_gd_consumer_pointer(void)
{
    return &tinos_gd_counter;
}
