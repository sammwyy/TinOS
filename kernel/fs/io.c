/* Asynchronous VFS reads. Device I/O may block inside a private worker, but
 * completion callbacks always run from the desktop's normal context. */

#include <tinos/fs.h>
#include <tinos/heap.h>
#include <tinos/string.h>
#ifndef TINOS_HOST
#include <tinos/thread.h>
#endif

struct fs_io_request {
    const fs_node_t *file;
    uint8_t *data;
    size_t size;
    bool ok;
    bool completed;
    bool notified;
    bool cancelled;
    bool released;
    struct fs_io_request *next;
    fs_io_complete_fn complete;
    void *arg;
#ifndef TINOS_HOST
    kernel_thread_t *worker;
#endif
};

static fs_io_request_t *requests;

#ifndef TINOS_HOST
static void io_worker(void *arg)
{
    fs_io_request_t *request = arg;
    if (!thread_cancelled() && !request->cancelled)
        request->ok = fs_read_at(request->file, 0, request->data,
                                 request->size);
    request->completed = true;
    if (request->cancelled && request->released) {
        kfree(request->data);
        kfree(request);
    }
    thread_exit();
}
#endif

fs_io_request_t *fs_read_all_async(const fs_node_t *file,
                                   fs_io_complete_fn complete, void *arg)
{
    if (!file || file->type != FS_FILE || !complete)
        return NULL;
    fs_io_request_t *request = kcalloc(1, sizeof(*request));
    if (!request)
        return NULL;
    request->file = file;
    request->size = file->size;
    request->complete = complete;
    request->arg = arg;
    request->next = requests;
    requests = request;
    if (request->size) {
        request->data = kmalloc(request->size);
        if (!request->data) {
            kfree(request);
            return NULL;
        }
    }
#ifdef TINOS_HOST
    request->ok = !request->size || fs_read_at(file, 0, request->data,
                                               request->size);
    request->completed = true;
#else
    request->worker = thread_spawn_pinned("fs-read", io_worker, request);
    if (!request->worker) {
        kfree(request->data);
        kfree(request);
        return NULL;
    }
#endif
    return request;
}

void fs_io_poll(void)
{
    for (fs_io_request_t *request = requests; request;) {
        fs_io_request_t *next = request->next;
        if (request->completed && !request->notified) {
            request->notified = true;
            request->complete(request, request->arg);
        }
        request = next;
    }
}

void fs_io_cancel(fs_io_request_t *request)
{
    if (!request)
        return;
#ifndef TINOS_HOST
    if (request->worker) {
        request->cancelled = true;
        thread_cancel(request->worker);
        thread_detach(request->worker);
    }
#endif
    request->completed = true;
}

bool fs_io_ok(const fs_io_request_t *request)
{
    return request && request->completed && request->ok;
}

const uint8_t *fs_io_data(const fs_io_request_t *request)
{
    return request ? request->data : NULL;
}

size_t fs_io_size(const fs_io_request_t *request)
{
    return request ? request->size : 0;
}

void fs_io_release(fs_io_request_t *request)
{
    if (!request)
        return;
    for (fs_io_request_t **link = &requests; *link; link = &(*link)->next) {
        if (*link == request) {
            *link = request->next;
            break;
        }
    }
#ifndef TINOS_HOST
    if (request->worker && thread_state(request->worker) != THREAD_DEAD) {
        request->released = true;
        return;
    }
#endif
    kfree(request->data);
    kfree(request);
}
