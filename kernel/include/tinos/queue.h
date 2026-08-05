/* Bounded pointer queues for producer/consumer kernel workers. */
#ifndef TINOS_QUEUE_H
#define TINOS_QUEUE_H

#include <tinos/sync.h>
#include <tinos/types.h>

typedef struct {
    void **items;
    size_t capacity;
    size_t head;
    size_t tail;
    ksemaphore_t available;
    ksemaphore_t space;
} kqueue_t;

void kqueue_init(kqueue_t *queue, void **storage, size_t capacity);
bool kqueue_try_push(kqueue_t *queue, void *item);
bool kqueue_try_pop(kqueue_t *queue, void **item);
void kqueue_push(kqueue_t *queue, void *item);
void *kqueue_pop(kqueue_t *queue);
size_t kqueue_count(const kqueue_t *queue);

#endif /* TINOS_QUEUE_H */
