/* Kernel synchronization for worker threads. */
#ifndef TINOS_SYNC_H
#define TINOS_SYNC_H

#include <tinos/thread.h>
#include <tinos/types.h>

typedef struct {
    kernel_thread_t *owner;
    thread_wait_queue_t waiters;
} kmutex_t;

typedef struct {
    bool signaled;
    thread_wait_queue_t waiters;
} kevent_t;

typedef struct {
    uint32_t count;
    uint32_t maximum;
    thread_wait_queue_t waiters;
} ksemaphore_t;

void kmutex_init(kmutex_t *mutex);
bool kmutex_try_lock(kmutex_t *mutex);
void kmutex_lock(kmutex_t *mutex);
void kmutex_unlock(kmutex_t *mutex);

void ksemaphore_init(ksemaphore_t *semaphore, uint32_t initial,
                     uint32_t maximum);
bool ksemaphore_try_wait(ksemaphore_t *semaphore);
void ksemaphore_wait(ksemaphore_t *semaphore);
void ksemaphore_post(ksemaphore_t *semaphore);

void kevent_init(kevent_t *event, bool signaled);
void kevent_wait(kevent_t *event);
void kevent_signal(kevent_t *event);
void kevent_reset(kevent_t *event);

#endif /* TINOS_SYNC_H */
