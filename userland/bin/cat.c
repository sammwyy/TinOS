#include <tinos/libc.h>

static void write_text(int fd, const char *text)
{
    unsigned length = 0;
    while (text[length])
        length++;
    tinos_write(fd, text, length);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        write_text(2, "usage: cat <absolute file>...\n");
        return 2;
    }

    for (int i = 1; i < argc; i++) {
        int fd = tinos_open(argv[i]);
        if (fd < 0) {
            write_text(2, "cat: cannot open ");
            write_text(2, argv[i]);
            write_text(2, "\n");
            return 1;
        }
        char buffer[512];
        int count;
        while ((count = tinos_read(fd, buffer, sizeof(buffer))) > 0) {
            for (int offset = 0; offset < count;) {
                int written = tinos_write(1, buffer + offset,
                                          (unsigned)(count - offset));
                if (written <= 0) {
                    tinos_close(fd);
                    return 1;
                }
                offset += written;
            }
        }
        tinos_close(fd);
        if (count < 0)
            return 1;
    }
    return 0;
}
