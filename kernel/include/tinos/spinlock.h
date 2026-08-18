/* Real cross-core spinlocks.
 *
 * irq_save()/irq_restore() alone (used everywhere before SMP existed) only
 * stops the *calling* core's own interrupts - it does nothing to stop a
 * second core from touching the same structure at the same time. Anything
 * an SMP application processor's scheduler loop can reach (the thread
 * pool, the heap, the physical frame allocator) needs one of these
 * instead.
 *
 * These never sleep: a thread must not hold one across a context switch,
 * a blocking call, or a kmalloc()/panic() that could recurse into another
 * lock - keep critical sections short and leaf-level.
 */
#ifndef TINOS_SPINLOCK_H
#define TINOS_SPINLOCK_H

#include <tinos/types.h>

typedef struct {
    volatile uint32_t locked;
} spinlock_t;

#define SPINLOCK_INIT { 0 }

void spin_init(spinlock_t *lock);

/* Disables the calling core's interrupts first, so an interrupt handler
 * on this same core can never spin forever waiting for a lock this same
 * core already holds, then spins on the shared cache line until it wins.
 * Returns the previous interrupt-enable state, to hand to the matching
 * spin_unlock_irqrestore(). */
bool spin_lock_irqsave(spinlock_t *lock);
void spin_unlock_irqrestore(spinlock_t *lock, bool irq_was_enabled);

#endif /* TINOS_SPINLOCK_H */
