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
    mutex->waiter = NULL;
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
        mutex->waiter = self;
        thread_block();
    }
    mutex->waiter = NULL;
    mutex->owner = self;
}

void kmutex_unlock(kmutex_t *mutex)
{
    kernel_thread_t *self = thread_current();
    KASSERT(self != NULL && mutex->owner == self);
    mutex->owner = NULL;
    if (mutex->waiter) {
        thread_wake(mutex->waiter);
        mutex->waiter = NULL;
    }
}

void kevent_init(kevent_t *event, bool signaled)
{
    event->signaled = signaled;
    event->waiter = NULL;
}

void kevent_wait(kevent_t *event)
{
    kernel_thread_t *self = thread_current();
    KASSERT(self != NULL);
    while (!event->signaled) {
        event->waiter = self;
        thread_block();
    }
    event->signaled = false; /* auto-reset event */
    event->waiter = NULL;
}

void kevent_signal(kevent_t *event)
{
    event->signaled = true;
    if (event->waiter)
        thread_wake(event->waiter);
}

void kevent_reset(kevent_t *event)
{
    event->signaled = false;
}
