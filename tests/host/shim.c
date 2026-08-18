/* Host-side implementations of the kernel services the portable filesystem
 * and shell code depends on, so those modules can be unit tested natively
 * under AddressSanitizer. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tinos/fs.h>
#include <tinos/audio.h>
#include <tinos/heap.h>
#include <tinos/speaker.h>
#include <tinos/net.h>
#include <tinos/sh.h>

/* Heap */

static size_t live_allocations;

bool audio_available(void) { return false; }
bool audio_play_doremi(void) { return false; }
size_t audio_stream_capacity_frames(void) { return 0; }

void speaker_beep(uint32_t frequency, uint32_t duration_ms)
{
    (void)frequency;
    (void)duration_ms;
}

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

/* Clock */

static uint32_t fake_clock = 1000;

uint32_t fs_clock_ms(void)
{
    return fake_clock += 5;
}

/* Logging and panics */

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

/* System information */

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

/* Networking is not part of the portable host test binary. */
bool net_available(void) { return false; }
const net_config_t *net_config(void)
{
    static net_config_t config;
    return &config;
}
void net_poll(void) {}
bool net_init(void) { return false; }
void net_format_address(uint32_t address, char *out, size_t size)
{
    snprintf(out, size, "%u.%u.%u.%u", (address >> 24) & 255,
             (address >> 16) & 255, (address >> 8) & 255, address & 255);
}
bool net_parse_address(const char *text, uint32_t *out)
{
    (void)text; (void)out; return false;
}
bool net_ping(uint32_t address, uint32_t timeout_ms, uint32_t *elapsed_ms)
{
    (void)address; (void)timeout_ms; (void)elapsed_ms; return false;
}
bool net_dns_lookup(const char *name, uint32_t *address, uint32_t timeout_ms)
{
    (void)name; (void)address; (void)timeout_ms; return false;
}
bool net_http_get(const char *host, const char *path, char *response,
                  size_t capacity, uint32_t timeout_ms)
{
    (void)host; (void)path; (void)response; (void)capacity;
    (void)timeout_ms; return false;
}
bool net_async_init(void) { return false; }
uint32_t net_async_submit(net_job_kind_t kind, const char *target,
                          const char *path)
{
    (void)kind; (void)target; (void)path; return 0;
}
bool net_async_take_result(net_job_result_t *result)
{
    (void)result; return false;
}
