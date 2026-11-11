/* Minimal userspace I/O API. No kernel-private types cross this boundary. */
#ifndef TINOS_LIBC_H
#define TINOS_LIBC_H

int tinos_open(const char *path);
int tinos_close(int fd);
int tinos_read(int fd, void *buffer, unsigned length);
int tinos_write(int fd, const void *buffer, unsigned length);
__attribute__((noreturn)) void tinos_exit(int status);

#endif /* TINOS_LIBC_H */
