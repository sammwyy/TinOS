/* Freestanding implementations of the C string/memory routines the kernel
 * relies on.  GCC emits calls to memcpy/memset/memmove/memcmp even in
 * freestanding mode, so these must exist with exactly these names.
 *
 * Not compiled into the host test harness (libc provides them there).
 */

#include <tinos/string.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t       *d = dst;
    const uint8_t *s = src;

    /* Copy whole words when both pointers share the same alignment. */
    if (((uintptr_t)d & 3) == ((uintptr_t)s & 3)) {
        while (n && ((uintptr_t)d & 3)) {
            *d++ = *s++;
            n--;
        }
        uint32_t       *d32 = (uint32_t *)d;
        const uint32_t *s32 = (const uint32_t *)s;
        while (n >= 4) {
            *d32++ = *s32++;
            n -= 4;
        }
        d = (uint8_t *)d32;
        s = (const uint8_t *)s32;
    }
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t       *d = dst;
    const uint8_t *s = src;

    if (d == s || n == 0)
        return dst;
    if (d < s)
        return memcpy(dst, src, n);

    /* Overlapping and destination is above the source: copy backwards. */
    d += n;
    s += n;
    while (n--)
        *--d = *--s;
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;
    uint8_t  v = (uint8_t)c;

    while (n && ((uintptr_t)d & 3)) {
        *d++ = v;
        n--;
    }
    uint32_t  word = (uint32_t)v * 0x01010101u;
    uint32_t *d32  = (uint32_t *)d;
    while (n >= 4) {
        *d32++ = word;
        n -= 4;
    }
    d = (uint8_t *)d32;
    while (n--)
        *d++ = v;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    while (n--) {
        if (*x != *y)
            return (int)*x - (int)*y;
        x++;
        y++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0)
        return 0;
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++))
        ;
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = '\0';
    return dst;
}

char *strcat(char *dst, const char *src)
{
    char *d = dst + strlen(dst);
    while ((*d++ = *src++))
        ;
    return dst;
}

char *strchr(const char *s, int c)
{
    for (; *s; s++)
        if (*s == (char)c)
            return (char *)s;
    return (c == '\0') ? (char *)s : NULL;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    for (;; s++) {
        if (*s == (char)c)
            last = s;
        if (!*s)
            break;
    }
    return (char *)last;
}

char *strstr(const char *hay, const char *needle)
{
    if (!*needle)
        return (char *)hay;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (!*n)
            return (char *)hay;
    }
    return NULL;
}
