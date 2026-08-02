/* Minimal printf-style formatting used across the kernel. */
#ifndef TINOS_PRINTF_H
#define TINOS_PRINTF_H

#include <tinos/kernel.h>
#include <tinos/types.h>

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int ksnprintf(char *buf, size_t size, const char *fmt, ...) PRINTFLIKE(3, 4);

#endif /* TINOS_PRINTF_H */
