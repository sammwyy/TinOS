/* Basic scalar types.
 *
 * The kernel is freestanding, so the fixed width types are defined here.
 * The portable subsystems (filesystem, shell) are also compiled natively by
 * the host test harness, which supplies the real C library headers instead.
 */
#ifndef TINOS_TYPES_H
#define TINOS_TYPES_H

#ifdef TINOS_HOST

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#else /* freestanding kernel build */

typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef signed short       int16_t;
typedef unsigned short     uint16_t;
typedef signed int         int32_t;
typedef unsigned int       uint32_t;
typedef signed long long   int64_t;
typedef unsigned long long uint64_t;

typedef unsigned int       size_t;
typedef signed int         ssize_t;
typedef unsigned int       uintptr_t;
typedef signed int         intptr_t;
typedef signed int         ptrdiff_t;

#define INT32_MAX  2147483647
#define INT32_MIN  (-2147483647 - 1)
#define UINT32_MAX 4294967295u
#define SIZE_MAX   UINT32_MAX

#ifndef __cplusplus
typedef _Bool bool;
#define true  1
#define false 0
#endif

#define NULL ((void *)0)

typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, t)   __builtin_va_arg(v, t)
#define va_copy(d, s)  __builtin_va_copy(d, s)

#endif /* TINOS_HOST */

#endif /* TINOS_TYPES_H */
