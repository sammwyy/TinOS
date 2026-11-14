#ifndef TINOS_RUNLOOP_H
#define TINOS_RUNLOOP_H

#include <tinos/types.h>

/* The UI tick is optional and must return promptly. True returns to kmain. */
typedef bool (*kernel_ui_tick_t)(void *context);

bool kernel_loop_step(kernel_ui_tick_t ui_tick, void *context);
void kernel_loop_run(kernel_ui_tick_t ui_tick, void *context);
/* Called from the timer interrupt: counts ticks spent halted in the loop. */
void kernel_loop_account_tick(void);
uint32_t kernel_loop_idle_ticks(void);

#endif /* TINOS_RUNLOOP_H */
