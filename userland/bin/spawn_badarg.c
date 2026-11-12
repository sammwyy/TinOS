#include <tinos/libc.h>

int main(void)
{
    static const char path[] = "/apps/hello.elf";
    const char *args[] = { path };
    const char *invalid[] = { (const char *)0x100000 };
    if (tinos_spawn("/README.txt", 1, args) != -1 ||
        tinos_spawn(path, 1, (const char *const *)0x100000) != -1 ||
        tinos_spawn(path, 1, invalid) != -1 ||
        tinos_spawn(path, 17, args) != -1 ||
        tinos_wait(42, (int *)0) != -1)
        return 1;

    static const char message[] = "ring3: invalid spawn arguments rejected\n";
    tinos_write(1, message, sizeof(message) - 1);
    return 0;
}
