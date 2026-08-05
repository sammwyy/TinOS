/* Kernel threads.
 *
 * Threads have private kernel stacks and are scheduled cooperatively for now.
 * A thread must call thread_yield() or thread_sleep() to give the desktop and
 * other workers time to run. The context-switch ABI is intentionally small so
 * the PIT can provide preemption in a later step.
 */
#ifndef TINOS_THREAD_H
#define TINOS_THREAD_H

#include <tinos/kernel.h>
#include <tinos/types.h>

#define THREAD_MAX       32
#define THREAD_STACK_SIZE (16u * 1024u)

typedef struct kernel_thread kernel_thread_t;
typedef void (*thread_entry_fn)(void *arg);

void thread_init(void);
kernel_thread_t *thread_spawn(const char *name, thread_entry_fn entry,
                              void *arg);
void thread_yield(void);
void thread_sleep(uint32_t milliseconds);
void thread_block(void);
void thread_wake(kernel_thread_t *thread);
void thread_exit(void) NORETURN;
kernel_thread_t *thread_current(void);

/* Run at most `budget` runnable threads and return to the caller after each
 * worker yields or exits. Must only be called from normal kernel context. */
size_t thread_run(size_t budget);
size_t thread_active(void);
uint64_t thread_next_wakeup(void);

#endif /* TINOS_THREAD_H */
