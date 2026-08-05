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
    uint32_t id;
    uint8_t state;
    bool used;
    char name[24];
};

enum {
    THREAD_READY = 0,
    THREAD_RUNNING,
    THREAD_SLEEPING,
    THREAD_BLOCKED,
    THREAD_DEAD,
};

extern void arch_context_switch(thread_context_t *from,
                                const thread_context_t *to);

static kernel_thread_t threads[THREAD_MAX];
static thread_context_t scheduler_context;
static kernel_thread_t *current;
static uint32_t next_id;

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
}

static void thread_bootstrap(void)
{
    kernel_thread_t *thread = current;
    thread->entry(thread->arg);
    thread_exit();
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
    current->state = THREAD_READY;
    kernel_thread_t *thread = current;
    arch_context_switch(&thread->context, &scheduler_context);
}

void thread_sleep(uint32_t milliseconds)
{
    if (!current)
        return;
    current->wake_at = timer_ticks() + milliseconds;
    current->state = THREAD_SLEEPING;
    kernel_thread_t *thread = current;
    arch_context_switch(&thread->context, &scheduler_context);
}

void thread_block(void)
{
    if (!current)
        return;
    current->state = THREAD_BLOCKED;
    kernel_thread_t *thread = current;
    arch_context_switch(&thread->context, &scheduler_context);
}

void thread_wake(kernel_thread_t *thread)
{
    if (thread && thread->used && thread->state == THREAD_BLOCKED)
        thread->state = THREAD_READY;
}

kernel_thread_t *thread_current(void)
{
    return current;
}

void thread_exit(void)
{
    KASSERT(current != NULL);
    kernel_thread_t *thread = current;
    thread->state = THREAD_DEAD;
    arch_context_switch(&thread->context, &scheduler_context);
    panic("thread %u resumed after exit", thread->id);
}

size_t thread_run(size_t budget)
{
    size_t ran = 0;
    uint64_t now = timer_ticks();
    if (budget == 0 || current)
        return 0;

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
        arch_context_switch(&scheduler_context, &thread->context);
        current = NULL;
        ran++;

        if (thread->state == THREAD_DEAD) {
            kfree(thread->stack);
            memset(thread, 0, sizeof(*thread));
        }
    }
    return ran;
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
