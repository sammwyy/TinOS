/* Deferred kernel timers. Callbacks run from normal scheduler context. */
#ifndef TINOS_TIMER_H
#define TINOS_TIMER_H

#include <tinos/types.h>

#define KTIMER_MAX 32

typedef struct ktimer ktimer_t;
typedef void (*ktimer_fn)(void *arg);

struct ktimer {
    uint64_t deadline;
    uint32_t period;
    ktimer_fn callback;
    void *arg;
    bool active;
};

void ktimer_init(void);
ktimer_t *ktimer_start(uint32_t delay, uint32_t period, ktimer_fn callback,
                       void *arg);
void ktimer_cancel(ktimer_t *timer);
size_t ktimer_poll(size_t budget);
uint64_t ktimer_next_deadline(void);

#endif /* TINOS_TIMER_H */
