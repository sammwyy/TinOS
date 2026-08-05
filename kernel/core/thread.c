/* Cooperative kernel threads with private stacks. */

#include <tinos/arch.h>
#include <tinos/heap.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/string.h>
#include <tinos/thread.h>

typedef struct {
    uint32_t ebx, esi, edi, ebp, esp, eip;
} thread_context_t;

struct kernel_thread {
    thread_context_t context;
    uint8_t *stack;
    thread_entry_fn entry;
    void *arg;
    uint64_t wake_at;
    regs_t irq_frame;
    regs_t *irq_frame_ptr;
    bool irq_frame_valid;
    uint32_t id;
    thread_state_t state;
    bool used;
    bool started;
    bool detached;
    bool cancel_requested;
    int exit_status;
    kernel_thread_t *joiner;
    thread_wait_queue_t joiners;
    kernel_thread_t *wait_next;
    thread_wait_queue_t *waiting_on;
    char name[24];
};

extern void arch_context_switch(thread_context_t *from,
                                const thread_context_t *to);
extern void arch_resume_irq(void);
extern void arch_resume_context(void);

static kernel_thread_t threads[THREAD_MAX];
static thread_context_t scheduler_context;
static kernel_thread_t *current;
static uint32_t next_id;
static bool preemption_enabled;
static bool scheduler_switching;
thread_context_t *thread_irq_resume_context;

static void context_return(void)
{
    kernel_thread_t *thread = current;
    thread->entry(thread->arg);
    thread_exit();
}

static void copy_name(char *dst, const char *src)
{
    size_t i = 0;
    if (!src)
        src = "thread";
    for (; i + 1 < 24 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

void thread_init(void)
{
    memset(threads, 0, sizeof(threads));
    memset(&scheduler_context, 0, sizeof(scheduler_context));
    current = NULL;
    next_id = 1;
    preemption_enabled = false;
    scheduler_switching = false;
    thread_irq_resume_context = NULL;
}

static void thread_bootstrap(void)
{
    scheduler_switching = false;
    context_return();
}

void thread_wait_queue_init(thread_wait_queue_t *queue)
{
    queue->head = NULL;
    queue->tail = NULL;
}

static void wait_remove(kernel_thread_t *thread)
{
    thread_wait_queue_t *queue = thread->waiting_on;
    if (!queue)
        return;
    kernel_thread_t *prev = NULL;
    for (kernel_thread_t *it = queue->head; it; it = it->wait_next) {
        if (it != thread) {
            prev = it;
            continue;
        }
        if (prev)
            prev->wait_next = it->wait_next;
        else
            queue->head = it->wait_next;
        if (queue->tail == it)
            queue->tail = prev;
        it->wait_next = NULL;
        it->waiting_on = NULL;
        return;
    }
    thread->waiting_on = NULL;
    thread->wait_next = NULL;
}

static void switch_to_scheduler(void)
{
    kernel_thread_t *thread = current;
    arch_context_switch(&thread->context, &scheduler_context);
}

kernel_thread_t *thread_spawn(const char *name, thread_entry_fn entry,
                              void *arg)
{
    if (!entry)
        return NULL;

    bool saved = irq_save();
    for (size_t i = 0; i < ARRAY_SIZE(threads); i++) {
        kernel_thread_t *thread = &threads[i];
        if (thread->used)
            continue;

        uint8_t *stack = (uint8_t *)kmalloc(THREAD_STACK_SIZE);
        if (!stack) {
            irq_restore(saved);
            return NULL;
        }
        memset(thread, 0, sizeof(*thread));
        thread->stack = stack;
        thread->entry = entry;
        thread->arg = arg;
        thread->id = next_id++;
        if (next_id == 0)
            next_id = 1;
        thread->state = THREAD_READY;
        thread->used = true;
        thread->started = false;
        thread->detached = false;
        thread->irq_frame.ds = GDT_KERNEL_DATA;
        thread->irq_frame.int_no = 32;
        thread->irq_frame.err_code = 0;
        thread->irq_frame.eip = (uint32_t)(uintptr_t)thread_bootstrap;
        thread->irq_frame.cs = GDT_KERNEL_CODE;
        thread->irq_frame.eflags = 0x202;
        thread->irq_frame.useresp = 0;
        thread->irq_frame.ss = GDT_KERNEL_DATA;
        thread->irq_frame_ptr = &thread->irq_frame;
        thread->irq_frame_valid = true;
        thread_wait_queue_init(&thread->joiners);
        copy_name(thread->name, name);

        uintptr_t top = (uintptr_t)stack + THREAD_STACK_SIZE;
        top &= ~(uintptr_t)0x0F;
        thread->context.esp = (uint32_t)top;
        thread->context.eip = (uint32_t)(uintptr_t)thread_bootstrap;
        irq_restore(saved);
        return thread;
    }
    irq_restore(saved);
    return NULL;
}

void thread_yield(void)
{
    if (!current)
        return;
    current->irq_frame_valid = false;
    current->state = THREAD_READY;
    switch_to_scheduler();
}

void thread_sleep(uint32_t milliseconds)
{
    if (!current)
        return;
    current->irq_frame_valid = false;
    current->wake_at = timer_ticks() + milliseconds;
    current->state = THREAD_SLEEPING;
    switch_to_scheduler();
}

void thread_block(void)
{
    if (!current)
        return;
    current->irq_frame_valid = false;
    current->state = THREAD_BLOCKED;
    switch_to_scheduler();
}

void thread_block_on(thread_wait_queue_t *queue)
{
    if (!current) {
        return;
    }
    current->irq_frame_valid = false;
    current->wait_next = NULL;
    current->waiting_on = queue;
    if (queue->tail)
        queue->tail->wait_next = current;
    else
        queue->head = current;
    queue->tail = current;
    current->state = THREAD_BLOCKED;
    switch_to_scheduler();
}

void thread_wake_one(thread_wait_queue_t *queue)
{
    kernel_thread_t *thread = queue->head;
    if (!thread)
        return;
    queue->head = thread->wait_next;
    if (queue->tail == thread)
        queue->tail = NULL;
    thread->wait_next = NULL;
    thread->waiting_on = NULL;
    if (thread->used && thread->state == THREAD_BLOCKED)
        thread->state = THREAD_READY;
}

void thread_wake_all(thread_wait_queue_t *queue)
{
    while (queue->head)
        thread_wake_one(queue);
}

void thread_wake(kernel_thread_t *thread)
{
    if (!thread || !thread->used)
        return;
    if (thread->waiting_on)
        wait_remove(thread);
    if (thread->state == THREAD_BLOCKED || thread->state == THREAD_SLEEPING)
        thread->state = THREAD_READY;
}

kernel_thread_t *thread_current(void)
{
    return current;
}

void thread_exit(void)
{
    thread_exit_code(0);
}

void thread_exit_code(int status)
{
    KASSERT(current != NULL);
    kernel_thread_t *thread = current;
    thread->exit_status = status;
    thread->state = THREAD_DEAD;
    thread_wake_all(&thread->joiners);
    switch_to_scheduler();
    panic("thread %u resumed after exit", thread->id);
}

int thread_join(kernel_thread_t *thread)
{
    KASSERT(thread != NULL && thread != current && thread->used);
    KASSERT(!thread->detached && thread->joiner == NULL);
    if (thread->state != THREAD_DEAD) {
        KASSERT(current != NULL);
        thread->joiner = current;
        while (thread->state != THREAD_DEAD)
            thread_block_on(&thread->joiners);
    }
    int status = thread->exit_status;
    thread->joiner = NULL;
    kfree(thread->stack);
    memset(thread, 0, sizeof(*thread));
    return status;
}

void thread_detach(kernel_thread_t *thread)
{
    if (!thread || !thread->used)
        return;
    thread->detached = true;
    if (thread->state == THREAD_DEAD) {
        kfree(thread->stack);
        memset(thread, 0, sizeof(*thread));
    }
}

void thread_cancel(kernel_thread_t *thread)
{
    if (!thread || !thread->used)
        return;
    thread->cancel_requested = true;
    thread_wake(thread);
}

bool thread_cancelled(void)
{
    return current && current->cancel_requested;
}

thread_state_t thread_state(const kernel_thread_t *thread)
{
    return thread ? thread->state : THREAD_DEAD;
}

size_t thread_run(size_t budget)
{
    size_t ran = 0;
    uint64_t now = timer_ticks();
    if (budget == 0 || current)
        return 0;
    preemption_enabled = true;

    for (size_t i = 0; i < ARRAY_SIZE(threads) && ran < budget; i++) {
        kernel_thread_t *thread = &threads[i];
        if (!thread->used)
            continue;
        if (thread->state == THREAD_SLEEPING) {
            if (now < thread->wake_at)
                continue;
            thread->state = THREAD_READY;
        }
        if (thread->state != THREAD_READY)
            continue;

        current = thread;
        thread->state = THREAD_RUNNING;
        thread->started = true;
        scheduler_switching = true;
        arch_context_switch(&scheduler_context, &thread->context);
        scheduler_switching = false;
        current = NULL;
        ran++;

        if (thread->state == THREAD_DEAD && thread->detached) {
            kfree(thread->stack);
            memset(thread, 0, sizeof(*thread));
        }
    }
    return ran;
}

static void prepare_irq_resume(kernel_thread_t *thread)
{
    uint32_t *slot = (uint32_t *)(thread->stack + THREAD_STACK_SIZE);
    --slot;
    *slot = (uint32_t)(uintptr_t)thread->irq_frame_ptr;
    thread->context.esp = (uint32_t)(uintptr_t)slot;
    thread->context.eip = (uint32_t)(uintptr_t)arch_resume_irq;
}

static void prepare_cooperative_frame(kernel_thread_t *thread)
{
    memset(&thread->irq_frame, 0, sizeof(thread->irq_frame));
    thread->irq_frame.ds = GDT_KERNEL_DATA;
    thread->irq_frame.int_no = 32;
    thread->irq_frame.eip = (uint32_t)(uintptr_t)arch_resume_context;
    thread->irq_frame.cs = GDT_KERNEL_CODE;
    thread->irq_frame.eflags = 0x002;
    thread->irq_frame.ss = GDT_KERNEL_DATA;
    thread->irq_frame_ptr = &thread->irq_frame;
    thread_irq_resume_context = &thread->context;
}

/* Timer IRQ entry point. The caller restores the returned frame with iret. */
regs_t *thread_preempt(regs_t *frame)
{
    if (!preemption_enabled || scheduler_switching || !current ||
        current->state != THREAD_RUNNING)
        return frame;

    kernel_thread_t *old = current;
    kernel_thread_t *next = NULL;

    for (size_t i = 0; i < ARRAY_SIZE(threads); i++) {
        kernel_thread_t *candidate = &threads[i];
        if (candidate->used && candidate != old &&
            candidate->state == THREAD_READY) {
            next = candidate;
            break;
        }
    }
    if (!next) {
        return frame;
    }

    old->irq_frame_ptr = frame;
    old->irq_frame_valid = true;
    prepare_irq_resume(old);
    old->state = THREAD_READY;

    current = next;
    next->state = THREAD_RUNNING;
    if (!next->started || !next->irq_frame_valid) {
        prepare_cooperative_frame(next);
        next->irq_frame_valid = false;
    }
    next->started = true;
    return next->irq_frame_ptr;
}

size_t thread_active(void)
{
    size_t count = 0;
    for (size_t i = 0; i < ARRAY_SIZE(threads); i++)
        if (threads[i].used && threads[i].state != THREAD_DEAD)
            count++;
    return count;
}

uint64_t thread_next_wakeup(void)
{
    uint64_t next = ~(uint64_t)0;
    for (size_t i = 0; i < ARRAY_SIZE(threads); i++)
        if (threads[i].used && threads[i].state == THREAD_SLEEPING &&
            threads[i].wake_at < next)
            next = threads[i].wake_at;
    return next;
}
