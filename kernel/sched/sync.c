/* Cooperative mutexes and events.
 *
 * These primitives deliberately block only the current kernel thread. They
 * never spin and never run callbacks from IRQ context, which is important for
 * NIC RX paths and makes the later preemptive scheduler easier to introduce.
 */

#include <tinos/kernel.h>
#include <tinos/sync.h>

void kmutex_init(kmutex_t *mutex)
{
    mutex->owner = NULL;
    thread_wait_queue_init(&mutex->waiters);
}

bool kmutex_try_lock(kmutex_t *mutex)
{
    kernel_thread_t *self = thread_current();
    if (!self || mutex->owner)
        return false;
    mutex->owner = self;
    return true;
}

void kmutex_lock(kmutex_t *mutex)
{
    kernel_thread_t *self = thread_current();
    KASSERT(self != NULL);
    KASSERT(mutex->owner != self); /* recursive locking is a bug for now */

    while (mutex->owner) {
        thread_block_on(&mutex->waiters);
    }
    mutex->owner = self;
}

void kmutex_unlock(kmutex_t *mutex)
{
    kernel_thread_t *self = thread_current();
    KASSERT(self != NULL && mutex->owner == self);
    mutex->owner = NULL;
    thread_wake_one(&mutex->waiters);
}

void ksemaphore_init(ksemaphore_t *semaphore, uint32_t initial,
                     uint32_t maximum)
{
    KASSERT(maximum > 0 && initial <= maximum);
    semaphore->count = initial;
    semaphore->maximum = maximum;
    thread_wait_queue_init(&semaphore->waiters);
}

bool ksemaphore_try_wait(ksemaphore_t *semaphore)
{
    if (!semaphore->count)
        return false;
    semaphore->count--;
    return true;
}

void ksemaphore_wait(ksemaphore_t *semaphore)
{
    KASSERT(thread_current() != NULL);
    while (!ksemaphore_try_wait(semaphore))
        thread_block_on(&semaphore->waiters);
}

void ksemaphore_post(ksemaphore_t *semaphore)
{
    if (semaphore->count < semaphore->maximum)
        semaphore->count++;
    thread_wake_one(&semaphore->waiters);
}

void kevent_init(kevent_t *event, bool signaled)
{
    event->signaled = signaled;
    thread_wait_queue_init(&event->waiters);
}

void kevent_wait(kevent_t *event)
{
    kernel_thread_t *self = thread_current();
    KASSERT(self != NULL);
    while (!event->signaled) {
        thread_block_on(&event->waiters);
    }
    event->signaled = false; /* auto-reset event */
}

void kevent_signal(kevent_t *event)
{
    event->signaled = true;
    thread_wake_one(&event->waiters);
}

void kevent_reset(kevent_t *event)
{
    event->signaled = false;
}
