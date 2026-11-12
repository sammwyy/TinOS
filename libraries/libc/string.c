#include <tinos/string.h>

size_t strlen(const char *s)
{
    size_t length = 0;
    while (s[length])
        length++;
    return length;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    return n ? (unsigned char)*a - (unsigned char)*b : 0;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s)
        return memcpy(dst, src, n);
    while (n--)
        d[n] = s[n];
    return dst;
}

void *memset(void *dst, int value, size_t n)
{
    unsigned char *d = dst;
    for (size_t i = 0; i < n; i++)
        d[i] = (unsigned char)value;
    return dst;
}

size_t tinos_strlcpy(char *dst, const char *src, size_t size)
{
    size_t length = strlen(src);
    if (size) {
        size_t copy = length < size ? length : size - 1;
        memcpy(dst, src, copy);
        dst[copy] = '\0';
    }
    return length;
}

void tinos_u32(char out[11], uint32_t value)
{
    char digits[10];
    unsigned count = 0;
    do {
        digits[count++] = '0' + value % 10;
        value /= 10;
    } while (value);
    for (unsigned i = 0; i < count; i++)
        out[i] = digits[count - i - 1];
    out[count] = '\0';
}
