/* printf-style formatting and a handful of string helpers.
 *
 * Shared between the kernel and the host test harness, so it must not touch
 * anything but <tinos/string.h> and the heap interface.
 */

#include <tinos/heap.h>
#include <tinos/printf.h>
#include <tinos/string.h>

typedef struct {
    char  *buf;
    size_t size; /* capacity including the NUL terminator */
    size_t len;  /* number of characters the full output would occupy */
} sbuf_t;

static void sb_putc(sbuf_t *sb, char c)
{
    if (sb->buf && sb->size && sb->len + 1 < sb->size)
        sb->buf[sb->len] = c;
    sb->len++;
}

static void sb_pad(sbuf_t *sb, char c, int count)
{
    for (int i = 0; i < count; i++)
        sb_putc(sb, c);
}

static void sb_puts(sbuf_t *sb, const char *s, int len)
{
    for (int i = 0; i < len; i++)
        sb_putc(sb, s[i]);
}

/* Render an unsigned value into buf (reversed by the caller). */
static int utoa_rev(char *out, uint64_t value, unsigned base, bool upper)
{
    static const char digits_lo[] = "0123456789abcdef";
    static const char digits_hi[] = "0123456789ABCDEF";
    const char       *digits      = upper ? digits_hi : digits_lo;
    int               n           = 0;

    if (value == 0)
        out[n++] = '0';
    while (value) {
        out[n++] = digits[value % base];
        value /= base;
    }
    return n;
}

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    sbuf_t sb = { buf, size, 0 };

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            sb_putc(&sb, *p);
            continue;
        }
        p++;
        if (*p == '\0')
            break;

        /* flags */
        bool left = false, zero = false, plus = false, space = false;
        for (;; p++) {
            if (*p == '-')
                left = true;
            else if (*p == '0')
                zero = true;
            else if (*p == '+')
                plus = true;
            else if (*p == ' ')
                space = true;
            else
                break;
        }

        /* field width */
        int width = 0;
        if (*p == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left  = true;
                width = -width;
            }
            p++;
        } else {
            while (*p >= '0' && *p <= '9')
                width = width * 10 + (*p++ - '0');
        }

        /* precision */
        int precision = -1;
        if (*p == '.') {
            p++;
            precision = 0;
            if (*p == '*') {
                precision = va_arg(ap, int);
                p++;
            } else {
                while (*p >= '0' && *p <= '9')
                    precision = precision * 10 + (*p++ - '0');
            }
        }

        /* length modifier */
        int longs = 0;
        while (*p == 'l') {
            longs++;
            p++;
        }
        if (*p == 'z' || *p == 'h') {
            p++;
        }

        char     conv = *p;
        char     num[32];
        int      nlen = 0;
        char     sign = 0;
        unsigned base = 10;
        bool     upper = false;
        bool     is_num = false;
        uint64_t uval   = 0;

        switch (conv) {
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (!left)
                sb_pad(&sb, ' ', width - 1);
            sb_putc(&sb, c);
            if (left)
                sb_pad(&sb, ' ', width - 1);
            continue;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            int slen = 0;
            while (s[slen] && (precision < 0 || slen < precision))
                slen++;
            if (!left)
                sb_pad(&sb, ' ', width - slen);
            sb_puts(&sb, s, slen);
            if (left)
                sb_pad(&sb, ' ', width - slen);
            continue;
        }
        case 'd':
        case 'i': {
            int64_t v = longs >= 2 ? va_arg(ap, int64_t) : (int64_t)va_arg(ap, int32_t);
            if (v < 0) {
                sign = '-';
                uval = (uint64_t)(-v);
            } else {
                uval = (uint64_t)v;
                if (plus)
                    sign = '+';
                else if (space)
                    sign = ' ';
            }
            is_num = true;
            break;
        }
        case 'u':
            uval   = longs >= 2 ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, uint32_t);
            is_num = true;
            break;
        case 'X':
            upper = true;
            /* fall through */
        case 'x':
            base   = 16;
            uval   = longs >= 2 ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, uint32_t);
            is_num = true;
            break;
        case 'o':
            base   = 8;
            uval   = (uint64_t)va_arg(ap, uint32_t);
            is_num = true;
            break;
        case 'p': {
            uintptr_t ptr = (uintptr_t)va_arg(ap, void *);
            sb_puts(&sb, "0x", 2);
            nlen = utoa_rev(num, ptr, 16, false);
            sb_pad(&sb, '0', 8 - nlen);
            while (nlen--)
                sb_putc(&sb, num[nlen]);
            continue;
        }
        case '%':
            sb_putc(&sb, '%');
            continue;
        default:
            sb_putc(&sb, '%');
            sb_putc(&sb, conv);
            continue;
        }

        if (is_num) {
            nlen     = utoa_rev(num, uval, base, upper);
            int digits = nlen;
            if (precision > digits)
                digits = precision;
            int total = digits + (sign ? 1 : 0);
            if (!left && !zero)
                sb_pad(&sb, ' ', width - total);
            if (sign)
                sb_putc(&sb, sign);
            if (!left && zero)
                sb_pad(&sb, '0', width - total);
            sb_pad(&sb, '0', digits - nlen);
            while (nlen--)
                sb_putc(&sb, num[nlen]);
            if (left)
                sb_pad(&sb, ' ', width - total);
        }
    }

    if (sb.buf && sb.size)
        sb.buf[sb.len < sb.size ? sb.len : sb.size - 1] = '\0';
    return (int)sb.len;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

/* String helpers */

size_t strlcpy(char *dst, const char *src, size_t size)
{
    size_t srclen = strlen(src);
    if (size) {
        size_t n = srclen < size - 1 ? srclen : size - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return srclen;
}

char *kstrndup(const char *s, size_t n)
{
    size_t len = 0;
    while (len < n && s[len])
        len++;
    char *copy = kmalloc(len + 1);
    if (!copy)
        return NULL;
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

char *kstrdup(const char *s)
{
    return kstrndup(s, SIZE_MAX);
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

int kstrcasecmp(const char *a, const char *b)
{
    while (*a && lower(*a) == lower(*b)) {
        a++;
        b++;
    }
    return (int)(uint8_t)lower(*a) - (int)(uint8_t)lower(*b);
}

const char *kstrcasestr(const char *hay, const char *needle)
{
    if (!*needle)
        return hay;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && lower(*h) == lower(*n)) {
            h++;
            n++;
        }
        if (!*n)
            return hay;
    }
    return NULL;
}

int katoi(const char *s)
{
    int sign = 1, value = 0;
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (*s >= '0' && *s <= '9')
        value = value * 10 + (*s++ - '0');
    return sign * value;
}
