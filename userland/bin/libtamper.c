#include <stdint.h>
#include <tinos/libc.h>

int main(void)
{
    int handle = tinos_dlopen("libtinosbmp.so");
    uint8_t *code = tinos_dlsym(handle, "tinos_bmp_decode");
    if (handle <= 0 || !code)
        return 1;
    *(volatile uint8_t *)code = 0xc3;
    return 2;
}
