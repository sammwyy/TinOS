extern unsigned version_one(void);
__asm__(".symver version_one,tinos_versioned@TINOS_1");

unsigned tinos_version_consume(void)
{
    return version_one();
}
