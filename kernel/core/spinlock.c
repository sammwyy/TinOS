#include <tinos/spinlock.h>
#include <tinos/io.h>

void spin_init(spinlock_t *lock)
{
    lock->locked = 0;
}

bool spin_lock_irqsave(spinlock_t *lock)
{
    bool was_enabled = irq_save();
    while (__atomic_exchange_n(&lock->locked, 1u, __ATOMIC_ACQUIRE))
        __asm__ volatile("pause");
    return was_enabled;
}

void spin_unlock_irqrestore(spinlock_t *lock, bool irq_was_enabled)
{
    __atomic_store_n(&lock->locked, 0u, __ATOMIC_RELEASE);
    irq_restore(irq_was_enabled);
}
