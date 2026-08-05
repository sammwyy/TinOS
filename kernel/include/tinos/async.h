/* Cooperative asynchronous tasks and workers.
 *
 * This is deliberately stackless: a task is a small state machine and must
 * return to the scheduler instead of blocking. The public contract is close
 * to a threaded worker API, so a future context-switch scheduler can replace
 * the implementation without changing network code.
 */
#ifndef TINOS_ASYNC_H
#define TINOS_ASYNC_H

#include <tinos/types.h>

#define ASYNC_MAX_TASKS 64
#define ASYNC_NAME_SIZE 24

typedef struct async_task async_task_t;

typedef enum {
    ASYNC_READY = 0,
    ASYNC_WAIT,
    ASYNC_DONE,
} async_result_t;

typedef async_result_t (*async_step_fn)(async_task_t *task);
typedef void (*async_work_fn)(void *arg);
typedef void (*async_cleanup_fn)(void *arg);

struct async_task {
    async_step_fn step;
    async_cleanup_fn cleanup;
    void *arg;
    uint64_t wake_at;
    uint32_t id;
    uint8_t state;
    bool used;
    bool cancelled;
    char name[ASYNC_NAME_SIZE];
};

void async_init(void);
async_task_t *async_spawn(const char *name, async_step_fn step, void *arg);
async_task_t *async_submit(const char *name, async_work_fn work, void *arg);
void async_cancel(async_task_t *task);
void async_wake(async_task_t *task);
void async_sleep(async_task_t *task, uint32_t milliseconds);
void async_yield(async_task_t *task);
size_t async_run(size_t budget);
size_t async_active(void);
uint64_t async_next_wakeup(void);

#endif /* TINOS_ASYNC_H */
