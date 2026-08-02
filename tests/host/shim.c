/* Host-side implementations of the kernel services the portable filesystem
 * and shell code depends on, so those modules can be unit tested natively
 * under AddressSanitizer. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tinos/fs.h>
#include <tinos/heap.h>
#include <tinos/sh.h>

/* --- heap ---------------------------------------------------------- */

static size_t live_allocations;

void *kmalloc(size_t size)
{
    live_allocations++;
    return malloc(size ? size : 1);
}

void *kcalloc(size_t count, size_t size)
{
    live_allocations++;
    return calloc(count ? count : 1, size ? size : 1);
}

void *krealloc(void *ptr, size_t size)
{
    if (!ptr)
        live_allocations++;
    return realloc(ptr, size ? size : 1);
}

void kfree(void *ptr)
{
    if (ptr)
        live_allocations--;
    free(ptr);
}

size_t host_live_allocations(void)
{
    return live_allocations;
}

void heap_get_stats(heap_stats_t *out)
{
    memset(out, 0, sizeof(*out));
    out->heap_total = 64u * 1024 * 1024;
    out->heap_used  = live_allocations * 64;
    out->heap_free  = out->heap_total - out->heap_used;
}

/* --- clock --------------------------------------------------------- */

static uint32_t fake_clock = 1000;

uint32_t fs_clock_ms(void)
{
    return fake_clock += 5;
}

/* --- logging and panics -------------------------------------------- */

void klog(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (getenv("TINOS_TEST_VERBOSE"))
        vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void panic(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "\nPANIC: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    abort();
}

/* --- system information -------------------------------------------- */

void sys_info_get(sys_info_t *out)
{
    memset(out, 0, sizeof(*out));
    out->uptime_ms = fs_clock_ms();
    snprintf(out->datetime, sizeof(out->datetime), "2000-01-01 00:00:00");
    snprintf(out->cpu, sizeof(out->cpu), "host test cpu");
    snprintf(out->platform, sizeof(out->platform), "host unit test");
    out->mem_total     = 256u * 1024 * 1024;
    out->mem_free      = 200u * 1024 * 1024;
    out->heap_total    = 64u * 1024 * 1024;
    out->heap_used     = 1024 * 1024;
    out->screen_width  = 1024;
    out->screen_height = 768;
}
