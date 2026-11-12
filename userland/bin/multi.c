#include <tinos/libc.h>

int main(void)
{
    static const char path[] = "/apps/hello.elf";
    const char *args[] = { path };
    int first = tinos_spawn(path, 1, args);
    int second = tinos_spawn(path, 1, args);
    if (first <= 0 || second <= first)
        return 1;

    int second_status, first_status;
    if (tinos_wait(second, &second_status) != second ||
        tinos_wait(first, &first_status) != first ||
        first_status != 17 || second_status != 17)
        return 1;
    static const char message[] = "ring3: both children collected\n";
    tinos_write(1, message, sizeof(message) - 1);
    return 0;
}
