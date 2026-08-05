/* Bounded pointer queue backed by counting semaphores. */

#include <tinos/kernel.h>
#include <tinos/queue.h>

void kqueue_init(kqueue_t *queue, void **storage, size_t capacity)
{
    KASSERT(queue != NULL && storage != NULL && capacity > 0);
    queue->items = storage;
    queue->capacity = capacity;
    queue->head = queue->tail = 0;
    ksemaphore_init(&queue->available, 0, capacity);
    ksemaphore_init(&queue->space, (uint32_t)capacity,
                    (uint32_t)capacity);
}

bool kqueue_try_push(kqueue_t *queue, void *item)
{
    if (!ksemaphore_try_wait(&queue->space))
        return false;
    queue->items[queue->head] = item;
    queue->head = (queue->head + 1) % queue->capacity;
    ksemaphore_post(&queue->available);
    return true;
}

bool kqueue_try_pop(kqueue_t *queue, void **item)
{
    if (!item || !ksemaphore_try_wait(&queue->available))
        return false;
    *item = queue->items[queue->tail];
    queue->tail = (queue->tail + 1) % queue->capacity;
    ksemaphore_post(&queue->space);
    return true;
}

void kqueue_push(kqueue_t *queue, void *item)
{
    KASSERT(thread_current() != NULL);
    ksemaphore_wait(&queue->space);
    queue->items[queue->head] = item;
    queue->head = (queue->head + 1) % queue->capacity;
    ksemaphore_post(&queue->available);
}

void *kqueue_pop(kqueue_t *queue)
{
    void *item;
    KASSERT(thread_current() != NULL);
    ksemaphore_wait(&queue->available);
    item = queue->items[queue->tail];
    queue->tail = (queue->tail + 1) % queue->capacity;
    ksemaphore_post(&queue->space);
    return item;
}

size_t kqueue_count(const kqueue_t *queue)
{
    return (size_t)queue->available.count;
}
