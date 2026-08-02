/* Growable byte streams: the plumbing behind pipelines and redirection. */

#include <tinos/heap.h>
#include <tinos/printf.h>
#include <tinos/sh.h>
#include <tinos/string.h>

void stream_init(stream_t *s)
{
    s->data     = NULL;
    s->size     = 0;
    s->capacity = 0;
}

void stream_free(stream_t *s)
{
    kfree(s->data);
    stream_init(s);
}

void stream_reset(stream_t *s)
{
    s->size = 0;
    if (s->data)
        s->data[0] = '\0';
}

static bool stream_grow(stream_t *s, size_t needed)
{
    if (s->capacity >= needed)
        return true;
    size_t capacity = s->capacity ? s->capacity : 128;
    while (capacity < needed)
        capacity *= 2;
    char *buf = krealloc(s->data, capacity);
    if (!buf)
        return false;
    s->data     = buf;
    s->capacity = capacity;
    return true;
}

void stream_write(stream_t *s, const void *data, size_t len)
{
    if (len == 0)
        return;
    if (!stream_grow(s, s->size + len + 1))
        return; /* out of memory: output is truncated rather than fatal */
    memcpy(s->data + s->size, data, len);
    s->size += len;
    s->data[s->size] = '\0';
}

void stream_puts(stream_t *s, const char *text)
{
    stream_write(s, text, strlen(text));
}

void stream_putc(stream_t *s, char c)
{
    stream_write(s, &c, 1);
}

void stream_printf(stream_t *s, const char *fmt, ...)
{
    char    buf[512];
    va_list ap;

    va_start(ap, fmt);
    int n = kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0)
        return;
    stream_write(s, buf, (size_t)n < sizeof(buf) - 1 ? (size_t)n
                                                     : sizeof(buf) - 1);
}

const char *stream_text(stream_t *s)
{
    return s->data ? s->data : "";
}

void stream_take(stream_t *dst, stream_t *src)
{
    stream_free(dst);
    *dst = *src;
    stream_init(src);
}
