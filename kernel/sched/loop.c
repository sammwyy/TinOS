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
    /* Keep deferred work bounded so input and drawing get a turn. */
    bus_dispatch(16);
    async_run(8);
    thread_run(8);
    process_schedule();
    fs_io_poll();
    ktimer_poll(4);
    return ui_tick && ui_tick(context);
}

void kernel_loop_run(kernel_ui_tick_t ui_tick, void *context)
{
    for (;;) {
        if (kernel_loop_step(ui_tick, context))
            return;
        arch_idle();
    }
}
