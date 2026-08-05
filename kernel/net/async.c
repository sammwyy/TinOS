/* Asynchronous network requests.
 *
 * Protocol code remains single-owner: this worker is the only context that
 * performs DNS, ICMP and TCP transactions after boot.  The desktop only
 * submits jobs and drains completed results, so a slow network peer cannot
 * stall input or rendering.
 */

#include <tinos/arch.h>
#include <tinos/heap.h>
#include <tinos/io.h>
#include <tinos/net.h>
#include <tinos/printf.h>
#include <tinos/string.h>
#include <tinos/thread.h>

#define NET_JOB_QUEUE 16
#define NET_JOB_TEXT  128
#define NET_JOB_PATH  256

typedef struct {
    uint32_t id;
    net_job_kind_t kind;
    char target[NET_JOB_TEXT];
    char path[NET_JOB_PATH];
} net_job_t;

static net_job_t jobs[NET_JOB_QUEUE];
static net_job_result_t results[NET_JOB_QUEUE];
static uint32_t job_head, job_tail, result_head, result_tail, next_id;
static kernel_thread_t *worker;

static bool ring_full(uint32_t head, uint32_t tail)
{
    return head - tail >= NET_JOB_QUEUE;
}

static void set_result(uint32_t id, bool success, const char *text)
{
    bool saved = irq_save();
    if (!ring_full(result_head, result_tail)) {
        net_job_result_t *result = &results[result_head++ % NET_JOB_QUEUE];
        result->id = id;
        result->success = success;
        strlcpy(result->text, text ? text : "", sizeof(result->text));
    }
    irq_restore(saved);
}

static void process_job(const net_job_t *job)
{
    char text[2048];
    uint32_t address = 0, elapsed = 0;
    char formatted[20];
    bool success = false;

    if (job->kind == NET_JOB_PING) {
        if (!net_parse_address(job->target, &address))
            success = net_dns_lookup(job->target, &address, 3000);
        else
            success = true;
        if (success)
            success = net_ping(address, 1000, &elapsed);
        if (success) {
            net_format_address(address, formatted, sizeof(formatted));
            ksnprintf(text, sizeof(text), "reply from %s (%s): time=%u ms\n",
                      job->target, formatted, elapsed);
        } else {
            ksnprintf(text, sizeof(text), "ping: timeout or resolution failure for %s\n",
                      job->target);
        }
    } else if (job->kind == NET_JOB_DNS) {
        success = net_dns_lookup(job->target, &address, 3000);
        if (success) {
            net_format_address(address, formatted, sizeof(formatted));
            ksnprintf(text, sizeof(text), "Name: %s\nAddress: %s\n",
                      job->target, formatted);
        } else {
            ksnprintf(text, sizeof(text), "nslookup: %s: no address found\n",
                      job->target);
        }
    } else {
        success = net_http_get(job->target, job->path, text, sizeof(text), 5000);
        if (!success)
            ksnprintf(text, sizeof(text), "httpget: request failed or timed out\n");
    }
    set_result(job->id, success, text);
}

static void net_worker(void *arg)
{
    UNUSED(arg);
    for (;;) {
        net_job_t job;
        bool have_job = false;
        bool saved = irq_save();
        if (job_tail != job_head) {
            job = jobs[job_tail++ % NET_JOB_QUEUE];
            have_job = true;
        }
        irq_restore(saved);
        if (have_job)
            process_job(&job);
        else
            thread_sleep(10);
    }
}

bool net_async_init(void)
{
    if (worker) return true;
    job_head = job_tail = result_head = result_tail = 0;
    next_id = 1;
    worker = thread_spawn("net-worker", net_worker, NULL);
    if (worker) thread_detach(worker);
    return worker != NULL;
}

uint32_t net_async_submit(net_job_kind_t kind, const char *target,
                          const char *path)
{
    if (!worker || !target || !target[0]) return 0;
    bool saved = irq_save();
    if (ring_full(job_head, job_tail)) {
        irq_restore(saved);
        return 0;
    }
    net_job_t *job = &jobs[job_head++ % NET_JOB_QUEUE];
    memset(job, 0, sizeof(*job));
    job->id = next_id++;
    if (!job->id) job->id = next_id++;
    job->kind = kind;
    strlcpy(job->target, target, sizeof(job->target));
    strlcpy(job->path, path && path[0] ? path : "/", sizeof(job->path));
    uint32_t id = job->id;
    irq_restore(saved);
    return id;
}

bool net_async_take_result(net_job_result_t *result)
{
    if (!result) return false;
    bool saved = irq_save();
    if (result_tail == result_head) {
        irq_restore(saved);
        return false;
    }
    *result = results[result_tail++ % NET_JOB_QUEUE];
    irq_restore(saved);
    return true;
}
