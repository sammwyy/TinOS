unsigned version_one(void)
{
    return 101;
}

unsigned version_two(void)
{
    return 202;
}

__asm__(".symver version_one,tinos_versioned@TINOS_1");
__asm__(".symver version_two,tinos_versioned@@TINOS_2");
