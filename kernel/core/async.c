/* Stackless cooperative task scheduler.
 * Tasks run only from normal kernel context, never from an ISR. */

#include <tinos/arch.h>
#include <tinos/async.h>
#include <tinos/heap.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/string.h>

typedef struct {
    async_work_fn work;
    void *arg;
} work_state_t;

static async_task_t tasks[ASYNC_MAX_TASKS];
static uint32_t next_id;

static void work_cleanup(void *arg)
{
    kfree(arg);
}

static void copy_name(char *dst, const char *src)
{
    size_t i = 0;
    if (!src)
        src = "task";
    for (; i + 1 < ASYNC_NAME_SIZE && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

void async_init(void)
{
    memset(tasks, 0, sizeof(tasks));
    next_id = 1;
}

async_task_t *async_spawn(const char *name, async_step_fn step, void *arg)
{
    if (!step)
        return NULL;

    bool saved = irq_save();
    for (size_t i = 0; i < ARRAY_SIZE(tasks); i++) {
        async_task_t *task = &tasks[i];
        if (task->used)
            continue;
        memset(task, 0, sizeof(*task));
        task->step = step;
        task->arg = arg;
        task->id = next_id++;
        if (next_id == 0)
            next_id = 1;
        task->used = true;
        task->state = ASYNC_READY;
        copy_name(task->name, name);
        irq_restore(saved);
        return task;
    }
    irq_restore(saved);
    return NULL;
}

static async_result_t work_step(async_task_t *task)
{
    work_state_t *work = (work_state_t *)task->arg;
    if (work && work->work)
        work->work(work->arg);
    task->arg = NULL;
    kfree(work);
    return ASYNC_DONE;
}

async_task_t *async_submit(const char *name, async_work_fn work, void *arg)
{
    if (!work)
        return NULL;
    work_state_t *state = kmalloc(sizeof(*state));
    if (!state)
        return NULL;
    state->work = work;
    state->arg = arg;
    async_task_t *task = async_spawn(name, work_step, state);
    if (!task)
        kfree(state);
    else
        task->cleanup = work_cleanup;
    return task;
}

void async_cancel(async_task_t *task)
{
    if (task && task->used) {
        task->cancelled = true;
        if (task->cleanup && task->arg) {
            task->cleanup(task->arg);
            task->arg = NULL;
        }
        task->used = false;
    }
}

void async_wake(async_task_t *task)
{
    if (task && task->used && !task->cancelled)
        task->state = ASYNC_READY;
}

void async_sleep(async_task_t *task, uint32_t milliseconds)
{
    if (!task || !task->used || task->cancelled)
        return;
    task->wake_at = timer_ticks() + milliseconds;
    task->state = ASYNC_WAIT;
}

void async_yield(async_task_t *task)
{
    if (task && task->used && !task->cancelled)
        task->state = ASYNC_READY;
}

size_t async_run(size_t budget)
{
    size_t ran = 0;
    uint64_t now = timer_ticks();
    if (budget == 0)
        return 0;

    for (size_t i = 0; i < ARRAY_SIZE(tasks) && ran < budget; i++) {
        async_task_t *task = &tasks[i];
        if (!task->used || task->cancelled)
            continue;
        if (task->state == ASYNC_WAIT) {
            if (now < task->wake_at)
                continue;
            task->state = ASYNC_READY;
        }
        if (task->state != ASYNC_READY)
            continue;

        async_result_t result = task->step(task);
        ran++;
        if (result == ASYNC_WAIT)
            task->state = ASYNC_WAIT;
        else if (result == ASYNC_DONE || task->cancelled) {
            if (task->cleanup && task->arg) {
                task->cleanup(task->arg);
                task->arg = NULL;
            }
            task->used = false;
        }
        else
            task->state = ASYNC_READY;
    }
    return ran;
}

size_t async_active(void)
{
    size_t count = 0;
    for (size_t i = 0; i < ARRAY_SIZE(tasks); i++)
        if (tasks[i].used && !tasks[i].cancelled)
            count++;
    return count;
}

uint64_t async_next_wakeup(void)
{
    uint64_t next = ~(uint64_t)0;
    for (size_t i = 0; i < ARRAY_SIZE(tasks); i++)
        if (tasks[i].used && tasks[i].state == ASYNC_WAIT &&
            tasks[i].wake_at < next)
            next = tasks[i].wake_at;
    return next;
}
