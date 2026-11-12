static unsigned counter = 9;

unsigned tinos_large_next(void)
{
    __asm__ volatile(".rept 72000\n nop\n .endr" ::: "memory");
    return ++counter;
}
