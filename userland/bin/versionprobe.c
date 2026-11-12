#include <tinos/libc.h>

extern unsigned version_one(void);
extern unsigned version_two(void);
__asm__(".symver version_one,tinos_versioned@TINOS_1");
__asm__(".symver version_two,tinos_versioned@TINOS_2");

int main(void)
{
    if (version_one() != 101 || version_two() != 202)
        return 1;
    int handle = tinos_dlopen("libtinosversioned.so");
    unsigned (*default_version)(void) =
        tinos_dlsym(handle, "tinos_versioned");
    unsigned (*old_version)(void) =
        tinos_dlvsym(handle, "tinos_versioned", "TINOS_1");
    unsigned (*new_version)(void) =
        tinos_dlvsym(handle, "tinos_versioned", "TINOS_2");
    if (handle < 1 || !default_version || default_version() != 202 ||
        !old_version || old_version() != 101 ||
        !new_version || new_version() != 202 ||
        tinos_dlvsym(handle, "tinos_versioned", "TINOS_3") ||
        tinos_dlclose(handle) != 0)
        return 2;
    const char message[] = "ring3: versioned ELF imports resolve correctly\n";
    tinos_write(1, message, sizeof(message) - 1);
    return 0;
}
