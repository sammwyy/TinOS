#include <tinos/libc.h>

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/README.txt";
    int first = tinos_open(path);
    int second = tinos_open(path);
    char a[8], b[8];
    int ok = first >= 3 && second > first &&
             tinos_read(first, a, sizeof(a)) == (int)sizeof(a) &&
             tinos_read(second, b, sizeof(b)) == (int)sizeof(b);
    for (unsigned i = 0; i < sizeof(a); i++)
        if (a[i] != b[i])
            ok = 0;
    char rest[512];
    ok = ok && tinos_read(first, rest, sizeof(rest)) > 0 &&
         tinos_read(first, rest, sizeof(rest)) == 0 &&
         tinos_write(first, "x", 1) == -1;
    ok = ok && tinos_close(first) == 0 &&
         tinos_read(first, rest, sizeof(rest)) == -1 &&
         tinos_close(second) == 0 &&
         tinos_open("/not-found") == -1;

    if (!ok)
        return 1;
    static const char message[] = "ring3: independent file descriptors\n";
    tinos_write(1, message, sizeof(message) - 1);
    return 0;
}
