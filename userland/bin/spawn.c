#include <tinos/libc.h>

static void message(const char *text)
{
    unsigned length = 0;
    while (text[length])
        length++;
    tinos_write(1, text, length);
}

int main(int argc, char **argv)
{
    if (argc < 2 || tinos_getpid() <= 0)
        return 2;
    int expected = 17;
    if (argc > 2) {
        expected = 0;
        for (const char *c = argv[2]; *c; c++) {
            if (*c < '0' || *c > '9')
                return 2;
            expected = expected * 10 + (*c - '0');
        }
    }

    const char *args[] = { argv[1] };
    int pid = tinos_spawn(argv[1], 1, args);
    if (pid <= 0)
        return 1;
    if (tinos_wait(pid, (int *)0x100000) != -1 ||
        tinos_wait(pid + 1000, 0) != -1)
        return 1;
    message("ring3: parent waiting for child\n");
    int status = -1;
    if (tinos_wait(pid, &status) != pid || status != expected)
        return 1;
    message("ring3: parent collected child\n");
    return 0;
}
