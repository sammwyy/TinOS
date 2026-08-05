/* Fixed-size deferred timer service. */

#include <tinos/arch.h>
#include <tinos/kernel.h>
#include <tinos/io.h>
#include <tinos/string.h>
#include <tinos/timer.h>

static ktimer_t timers[KTIMER_MAX];

void ktimer_init(void)
{
    memset(timers, 0, sizeof(timers));
}

ktimer_t *ktimer_start(uint32_t delay, uint32_t period, ktimer_fn callback,
                       void *arg)
{
    if (!callback)
        return NULL;
    bool saved = irq_save();
    for (size_t i = 0; i < ARRAY_SIZE(timers); i++) {
        if (timers[i].active)
            continue;
        timers[i].deadline = timer_ticks() + delay;
        timers[i].period = period;
        timers[i].callback = callback;
        timers[i].arg = arg;
        timers[i].active = true;
        irq_restore(saved);
        return &timers[i];
    }
    irq_restore(saved);
    return NULL;
}

void ktimer_cancel(ktimer_t *timer)
{
    if (timer)
        timer->active = false;
}

size_t ktimer_poll(size_t budget)
{
    size_t fired = 0;
    uint64_t now = timer_ticks();
    for (size_t i = 0; i < ARRAY_SIZE(timers) && fired < budget; i++) {
        ktimer_t *timer = &timers[i];
        if (!timer->active || now < timer->deadline)
            continue;
        ktimer_fn callback = timer->callback;
        void *arg = timer->arg;
        if (timer->period)
            timer->deadline += timer->period;
        else
            timer->active = false;
        callback(arg);
        fired++;
    }
    return fired;
}

uint64_t ktimer_next_deadline(void)
{
    uint64_t next = ~(uint64_t)0;
    for (size_t i = 0; i < ARRAY_SIZE(timers); i++)
        if (timers[i].active && timers[i].deadline < next)
            next = timers[i].deadline;
    return next;
}
