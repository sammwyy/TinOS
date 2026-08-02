/* Freestanding subset of <string.h> plus a few kernel helpers.
 *
 * When the portable code is compiled by the host test harness the real libc
 * declarations are used instead; the signatures below match them exactly.
 */
#ifndef TINOS_STRING_H
#define TINOS_STRING_H

#include <tinos/types.h>

#ifdef TINOS_HOST
#include <string.h>
#else
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
void  *memset(void *dst, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
char  *strcat(char *dst, const char *src);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *hay, const char *needle);
#endif

/* Kernel helpers (available in both builds, implemented in lib/printf.c). */
size_t strlcpy(char *dst, const char *src, size_t size);
char  *kstrdup(const char *s);
char  *kstrndup(const char *s, size_t n);
int    kstrcasecmp(const char *a, const char *b);
const char *kstrcasestr(const char *hay, const char *needle);
int    katoi(const char *s);

#endif /* TINOS_STRING_H */
