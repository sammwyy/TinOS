/* Kernel synchronization for worker threads. */
#ifndef TINOS_SYNC_H
#define TINOS_SYNC_H

#include <tinos/thread.h>
#include <tinos/types.h>

typedef struct {
    kernel_thread_t *owner;
    kernel_thread_t *waiter;
} kmutex_t;

typedef struct {
    bool signaled;
    kernel_thread_t *waiter;
} kevent_t;

void kmutex_init(kmutex_t *mutex);
bool kmutex_try_lock(kmutex_t *mutex);
void kmutex_lock(kmutex_t *mutex);
void kmutex_unlock(kmutex_t *mutex);

void kevent_init(kevent_t *event, bool signaled);
void kevent_wait(kevent_t *event);
void kevent_signal(kevent_t *event);
void kevent_reset(kevent_t *event);

#endif /* TINOS_SYNC_H */
