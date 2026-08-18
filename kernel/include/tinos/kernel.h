/* Kernel-wide helpers: logging, assertions, panics and small utilities. */
#ifndef TINOS_KERNEL_H
#define TINOS_KERNEL_H

#include <tinos/types.h>

#define TINOS_NAME    "TinOS"
#define TINOS_VERSION "1.0.0"

#define ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b)      ((a) < (b) ? (a) : (b))
#define MAX(a, b)      ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) MAX(lo, MIN(v, hi))
#define ALIGN_UP(v, a)   (((v) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(v, a) ((v) & ~((a) - 1))

#define PACKED     __attribute__((packed))
#define NORETURN   __attribute__((noreturn))
#define PRINTFLIKE(f, a) __attribute__((format(printf, f, a)))
#define UNUSED(x)  ((void)(x))

/* Kernel log: always written to the COM1 serial port, and mirrored to the
 * text-mode console while the machine is still in text mode. */
void klog(const char *fmt, ...) PRINTFLIKE(1, 2);
/* Pre-formatted variant klog() itself uses, exposed for callers (the native
 * app ABI) that cannot forward a variadic function through a pointer. */
void klog_str(const char *text);

/* Unrecoverable error: paints a diagnostic screen and halts the CPU. */
NORETURN void panic(const char *fmt, ...) PRINTFLIKE(1, 2);

#define KASSERT(cond)                                                     \
    do {                                                                  \
        if (!(cond))                                                      \
            panic("assertion failed: %s (%s:%d)", #cond, __FILE__,        \
                  __LINE__);                                              \
    } while (0)

#endif /* TINOS_KERNEL_H */
