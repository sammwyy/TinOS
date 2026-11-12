#include <tinos/libc.h>

int main(void)
{
    char line[81];
    for (unsigned i = 0; i < 80; i++)
        line[i] = 'a' + i % 26;
    line[80] = '\n';
    for (unsigned i = 0; i < 120; i++) {
        unsigned written = 0;
        while (written < sizeof(line)) {
            int result = tinos_write(1, line + written,
                                     sizeof(line) - written);
            if (result <= 0)
                return 1;
            written += (unsigned)result;
        }
    }
    const char message[] = "ring3: stream complete\n";
    return tinos_write(1, message, sizeof(message) - 1) ==
           (int)sizeof(message) - 1 ? 0 : 1;
}
