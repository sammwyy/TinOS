#include <tinos/arch.h>
#include <tinos/async.h>
#include <tinos/audio.h>
#include <tinos/bus.h>
#include <tinos/fs.h>
#include <tinos/process.h>
#include <tinos/runloop.h>
#include <tinos/thread.h>
#include <tinos/timer.h>

bool kernel_loop_step(kernel_ui_tick_t ui_tick, void *context)
{
    audio_poll();
    process_audio_wake();
    /* Keep deferred work bounded so input and drawing get a turn. */
    bus_dispatch(16);
    async_run(8);
    thread_run(8);
    process_schedule();
    fs_io_poll();
    ktimer_poll(4);
    return ui_tick && ui_tick(context);
}

static volatile bool loop_halted;
static volatile uint32_t idle_ticks;

void kernel_loop_account_tick(void)
{
    if (loop_halted)
        idle_ticks++;
}

uint32_t kernel_loop_idle_ticks(void)
{
    return idle_ticks;
}

void kernel_loop_run(kernel_ui_tick_t ui_tick, void *context)
{
    for (;;) {
        if (kernel_loop_step(ui_tick, context))
            return;
        loop_halted = true;
        arch_idle();
        loop_halted = false;
    }
}
