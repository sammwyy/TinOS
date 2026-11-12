#include <tinos/libc.h>

int main(void)
{
    int handle = tinos_dlopen("libtinosversionconsumer.so");
    unsigned (*consume)(void) = tinos_dlsym(handle, "tinos_version_consume");
    if (handle < 1 || !consume || consume() != 101 ||
        tinos_dlclose(handle) != 0)
        return 1;
    const char message[] = "ring3: versioned library dependency works\n";
    tinos_write(1, message, sizeof(message) - 1);
    return 0;
}
