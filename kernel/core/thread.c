/* Kernel threads: private stacks, voluntary scheduling, PIT-driven
 * preemption on the boot processor, and cooperative scheduling on any SMP
 * application processor (see smp.c:ap_entry()).
 *
 * The thread pool (threads[]) and every field two cores could touch at
 * once are protected by pool_lock, a real spinlock - irq_save()/
 * irq_restore() alone only ever stopped *this* core's own interrupts, so
 * once a second core can be running thread_run()/thread_preempt() at the
 * same time it is not enough. "current" and "the context to return to
 * when a thread yields" are inherently per-core, so they are now arrays
 * indexed by local APIC ID rather than single globals.
 *
 * thread_irq_resume_context stays a single global: it is only ever
 * touched by the timer-preemption path (thread_preempt()), and PIT
 * interrupts are only ever delivered to the boot processor (the legacy
 * 8259 PIC has no notion of a second core), so that path never runs on an
 * AP in the first place.
 */

#include <tinos/arch.h>
#include <tinos/heap.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/lapic.h>
#include <tinos/smp.h>
#include <tinos/spinlock.h>
#include <tinos/string.h>
#include <tinos/thread.h>

#define SMP_MAX_CPUS 32 /* matches ACPI_MAX_CPUS (tinos/acpi.h) */

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
    bool bsp_only; /* see thread_spawn_pinned() */
    int exit_status;
    kernel_thread_t *joiner;
    thread_wait_queue_t joiners;
    kernel_thread_t *wait_next;
    thread_wait_queue_t *waiting_on;
    char name[24];
    /* FXSAVE area (x87 + MMX + SSE/SSE2 state, 512 bytes, 16-byte aligned
     * as FXSAVE/FXRSTOR require). Threads that never touch SSE just carry
     * this around unused. */
    uint8_t fpu[512] __attribute__((aligned(16)));
};

extern void arch_context_switch(thread_context_t *from,
                                const thread_context_t *to);
extern void arch_resume_irq(void);
extern void arch_resume_context(void);

/* A freshly spawned thread has never run, so it has no saved FPU/SSE state
 * to restore - the CPU's current register contents simply carry over
 * untouched, which is safe since the thread cannot have read them yet. */
static inline void fpu_save(void *buf)
{
    __asm__ volatile("fxsave (%0)" : : "r"(buf) : "memory");
}

static inline void fpu_restore(const void *buf)
{
    __asm__ volatile("fxrstor (%0)" : : "r"(buf) : "memory");
}

static kernel_thread_t threads[THREAD_MAX];
static spinlock_t      pool_lock;
static uint32_t        next_id;
static bool            preemption_enabled; /* boot-processor only */
static size_t          scheduler_cursor;
thread_context_t *thread_irq_resume_context; /* boot-processor only, see above */

/* Per-CPU scheduling state, indexed by local APIC ID. QEMU (and every
 * machine this targets) hands out small, dense, 0-based APIC IDs, so no
 * separate lookup table is needed. */
static kernel_thread_t  *current_by_cpu[SMP_MAX_CPUS];
static thread_context_t  scheduler_context_by_cpu[SMP_MAX_CPUS];
static bool               switching_by_cpu[SMP_MAX_CPUS];

static inline size_t cpu_index(void)
{
    uint32_t id = lapic_id();
    return id < SMP_MAX_CPUS ? id : 0;
}

#define current             (current_by_cpu[cpu_index()])
#define scheduler_context    (scheduler_context_by_cpu[cpu_index()])
#define scheduler_switching  (switching_by_cpu[cpu_index()])

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
    /* Every core's slot is reset here, up front, on the boot processor -
     * this must not itself depend on cpu_index()/lapic_id(), since it
     * runs before smp_init() has brought up even this core's own local
     * APIC. */
    memset(threads, 0, sizeof(threads));
    memset(current_by_cpu, 0, sizeof(current_by_cpu));
    memset(scheduler_context_by_cpu, 0, sizeof(scheduler_context_by_cpu));
    memset(switching_by_cpu, 0, sizeof(switching_by_cpu));
    spin_init(&pool_lock);
    next_id = 1;
    preemption_enabled = false;
    scheduler_cursor = 0;
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

/* Callers must hold pool_lock. */
static void wait_remove_locked(kernel_thread_t *thread)
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

/* Never call with pool_lock held: a thread giving up its own core must not
 * carry a lock across the switch, or nothing could ever release it. */
static void switch_to_scheduler(void)
{
    kernel_thread_t *thread = current;
    fpu_save(thread->fpu);
    arch_context_switch(&thread->context, &scheduler_context);
    fpu_restore(thread->fpu);
}

static kernel_thread_t *thread_spawn_internal(const char *name,
                                              thread_entry_fn entry,
                                              void *arg, bool bsp_only)
{
    if (!entry)
        return NULL;

    uint8_t *stack = (uint8_t *)kmalloc(THREAD_STACK_SIZE);
    if (!stack)
        return NULL;

    bool saved = spin_lock_irqsave(&pool_lock);
    for (size_t i = 0; i < ARRAY_SIZE(threads); i++) {
        kernel_thread_t *thread = &threads[i];
        if (thread->used)
            continue;

        memset(thread, 0, sizeof(*thread));
        thread->stack = stack;
        thread->entry = entry;
        thread->arg = arg;
        thread->bsp_only = bsp_only;
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
        spin_unlock_irqrestore(&pool_lock, saved);
        return thread;
    }
    spin_unlock_irqrestore(&pool_lock, saved);
    kfree(stack);
    return NULL;
}

kernel_thread_t *thread_spawn(const char *name, thread_entry_fn entry,
                              void *arg)
{
    return thread_spawn_internal(name, entry, arg, false);
}

kernel_thread_t *thread_spawn_pinned(const char *name, thread_entry_fn entry,
                                     void *arg)
{
    return thread_spawn_internal(name, entry, arg, true);
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
    if (!current)
        return;

    bool saved = spin_lock_irqsave(&pool_lock);
    current->irq_frame_valid = false;
    current->wait_next = NULL;
    current->waiting_on = queue;
    if (queue->tail)
        queue->tail->wait_next = current;
    else
        queue->head = current;
    queue->tail = current;
    current->state = THREAD_BLOCKED;
    spin_unlock_irqrestore(&pool_lock, saved);

    switch_to_scheduler();
}

void thread_wake_one(thread_wait_queue_t *queue)
{
    bool saved = spin_lock_irqsave(&pool_lock);
    kernel_thread_t *thread = queue->head;
    if (thread) {
        queue->head = thread->wait_next;
        if (queue->tail == thread)
            queue->tail = NULL;
        thread->wait_next = NULL;
        thread->waiting_on = NULL;
        if (thread->used && thread->state == THREAD_BLOCKED)
            thread->state = THREAD_READY;
    }
    spin_unlock_irqrestore(&pool_lock, saved);
}

void thread_wake_all(thread_wait_queue_t *queue)
{
    bool saved = spin_lock_irqsave(&pool_lock);
    while (queue->head) {
        kernel_thread_t *thread = queue->head;
        queue->head = thread->wait_next;
        if (queue->tail == thread)
            queue->tail = NULL;
        thread->wait_next = NULL;
        thread->waiting_on = NULL;
        if (thread->used && thread->state == THREAD_BLOCKED)
            thread->state = THREAD_READY;
    }
    spin_unlock_irqrestore(&pool_lock, saved);
}

void thread_wake(kernel_thread_t *thread)
{
    if (!thread || !thread->used)
        return;
    bool saved = spin_lock_irqsave(&pool_lock);
    if (thread->waiting_on)
        wait_remove_locked(thread);
    if (thread->state == THREAD_BLOCKED || thread->state == THREAD_SLEEPING)
        thread->state = THREAD_READY;
    spin_unlock_irqrestore(&pool_lock, saved);
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

    bool saved = spin_lock_irqsave(&pool_lock);
    thread->exit_status = status;
    thread->state = THREAD_DEAD;
    spin_unlock_irqrestore(&pool_lock, saved);

    thread_wake_all(&thread->joiners);
    switch_to_scheduler();
    panic("thread %u resumed after exit", thread->id);
}

int thread_join(kernel_thread_t *thread)
{
    KASSERT(thread != NULL && thread != current && thread->used);
    KASSERT(!thread->detached && thread->joiner == NULL);
    if (thread_state(thread) != THREAD_DEAD) {
        KASSERT(current != NULL);
        bool saved = spin_lock_irqsave(&pool_lock);
        thread->joiner = current;
        spin_unlock_irqrestore(&pool_lock, saved);
        while (thread_state(thread) != THREAD_DEAD)
            thread_block_on(&thread->joiners);
    }

    bool saved = spin_lock_irqsave(&pool_lock);
    int status = thread->exit_status;
    thread->joiner = NULL;
    uint8_t *stack = thread->stack;
    memset(thread, 0, sizeof(*thread));
    spin_unlock_irqrestore(&pool_lock, saved);

    kfree(stack);
    return status;
}

void thread_detach(kernel_thread_t *thread)
{
    if (!thread || !thread->used)
        return;

    bool saved = spin_lock_irqsave(&pool_lock);
    thread->detached = true;
    bool dead = thread->state == THREAD_DEAD;
    uint8_t *stack = dead ? thread->stack : NULL;
    if (dead)
        memset(thread, 0, sizeof(*thread));
    spin_unlock_irqrestore(&pool_lock, saved);

    if (stack)
        kfree(stack);
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

/* Scans for one READY thread starting at scheduler_cursor, claims it
 * (THREAD_RUNNING) and advances the cursor, all under pool_lock so two
 * cores can never claim the same thread. this_bsp tells it whether
 * bsp_only threads are eligible here. Returns NULL if nothing is ready. */
static kernel_thread_t *claim_ready_thread(uint64_t now, bool this_is_bsp)
{
    bool saved = spin_lock_irqsave(&pool_lock);
    kernel_thread_t *found = NULL;
    for (size_t scan = 0; scan < ARRAY_SIZE(threads); scan++) {
        size_t index = (scheduler_cursor + scan) % ARRAY_SIZE(threads);
        kernel_thread_t *thread = &threads[index];
        if (!thread->used)
            continue;
        if (thread->bsp_only && !this_is_bsp)
            continue;
        if (thread->state == THREAD_SLEEPING) {
            if (now < thread->wake_at)
                continue;
            thread->state = THREAD_READY;
        }
        if (thread->state != THREAD_READY)
            continue;

        thread->state = THREAD_RUNNING;
        scheduler_cursor = (index + 1) % ARRAY_SIZE(threads);
        found = thread;
        break;
    }
    spin_unlock_irqrestore(&pool_lock, saved);
    return found;
}

size_t thread_run(size_t budget)
{
    size_t ran = 0;
    if (budget == 0 || current)
        return 0;

    bool this_is_bsp = lapic_id() == smp_bsp_apic_id();
    if (this_is_bsp)
        preemption_enabled = true;
    uint64_t now = timer_ticks();

    while (ran < budget) {
        kernel_thread_t *thread = claim_ready_thread(now, this_is_bsp);
        if (!thread)
            break;

        /* scheduler_switching must go true before `current` is published:
         * thread_preempt() (on another core, or this same core via a
         * nested interrupt) uses "current set && scheduler_switching
         * clear" to know a thread is genuinely running on its own stack.
         * Setting `current` first left a window where a timer tick could
         * see this thread as "current" and preempt it while its stack
         * switch (the arch_context_switch() below) had not happened yet -
         * capturing an interrupt frame that pointed at the *caller's*
         * stack, not the thread's, and corrupting the resume later. */
        scheduler_switching = true;
        current = thread;
        /* Only a thread resuming mid-instruction after a timer preemption
         * needs its FPU state restored here - a first-ever run has none
         * to restore, and a thread resuming from a cooperative yield
         * restores its own state right after arch_context_switch() in
         * switch_to_scheduler(). Once claimed, `thread` belongs
         * exclusively to this core, so these fields are safe to read
         * without the lock. */
        bool resuming_irq = thread->started && thread->irq_frame_valid;
        thread->started = true;
        if (resuming_irq)
            fpu_restore(thread->fpu);

        arch_context_switch(&scheduler_context, &thread->context);
        scheduler_switching = false;
        current = NULL;
        ran++;

        bool saved = spin_lock_irqsave(&pool_lock);
        bool cleanup = thread->state == THREAD_DEAD && thread->detached;
        uint8_t *stack = cleanup ? thread->stack : NULL;
        if (cleanup)
            memset(thread, 0, sizeof(*thread));
        spin_unlock_irqrestore(&pool_lock, saved);
        if (stack)
            kfree(stack);
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

/* Timer IRQ entry point - boot processor only (see the file comment).
 * The caller restores the returned frame with iret. */
regs_t *thread_preempt(regs_t *frame)
{
    if (!preemption_enabled || scheduler_switching || !current ||
        current->state != THREAD_RUNNING)
        return frame;

    kernel_thread_t *old = current;

    bool saved = spin_lock_irqsave(&pool_lock);
    kernel_thread_t *next = NULL;
    for (size_t i = 0; i < ARRAY_SIZE(threads); i++) {
        kernel_thread_t *candidate = &threads[i];
        if (candidate->used && candidate != old &&
            candidate->state == THREAD_READY) {
            next = candidate;
            next->state = THREAD_RUNNING;
            break;
        }
    }
    if (next)
        old->state = THREAD_READY;
    spin_unlock_irqrestore(&pool_lock, saved);

    if (!next)
        return frame;

    /* `old` is mid-instruction (interrupted by the timer), so its FPU/SSE
     * state is live in the hardware registers right now and must be
     * captured before anything else can touch them. Both threads are now
     * exclusively owned by this core (READY->RUNNING was claimed under
     * the lock above), so the rest needs no further locking. */
    fpu_save(old->fpu);

    old->irq_frame_ptr = frame;
    old->irq_frame_valid = true;
    prepare_irq_resume(old);

    current = next;
    if (!next->started || !next->irq_frame_valid) {
        /* Fresh thread, or resuming a cooperative yield: switch_to_scheduler()
         * (or nothing, for a never-started thread) owns restoring FPU state
         * for these cases - see the comment there. */
        prepare_cooperative_frame(next);
        next->irq_frame_valid = false;
    } else {
        fpu_restore(next->fpu);
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
