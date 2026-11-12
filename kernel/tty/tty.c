#include <tinos/arch.h>
#include <tinos/async.h>
#include <tinos/audio.h>
#include <tinos/bus.h>
#include <tinos/console.h>
#include <tinos/fs.h>
#include <tinos/input.h>
#include <tinos/kernel.h>
#include <tinos/process.h>
#include <tinos/sh.h>
#include <tinos/thread.h>
#include <tinos/timer.h>
#include <tinos/tty.h>

#define TTY_LINE_MAX 512

static void tty_prompt(shell_t *sh)
{
    vga_text_printf("%s $ ", shell_cwd(sh));
}

static bool tty_execute(shell_t *sh, const char *line)
{
    stream_t out;
    stream_init(&out);
    shell_execute(sh, line, &out);
    vga_text_write(stream_text(&out));
    stream_free(&out);

    char arg[FS_PATH_MAX];
    switch (shell_take_request(sh, arg, sizeof(arg))) {
    case SH_REQ_DESKTOP:
        return true;
    case SH_REQ_CLEAR:
        vga_text_clear();
        break;
    case SH_REQ_REBOOT:
        arch_reboot();
        break;
    case SH_REQ_SHUTDOWN:
        arch_shutdown();
        break;
    case SH_REQ_EXIT:
        vga_text_write("Use 'desktop' to start the graphical session.\n");
        break;
    case SH_REQ_EXPLORER:
    case SH_REQ_EDITOR:
    case SH_REQ_ABOUT:
    case SH_REQ_SYSINFO:
        vga_text_write("This command requires the desktop. Run 'desktop'.\n");
        break;
    case SH_REQ_NONE:
        break;
    }
    return false;
}

void tty_run(void)
{
    shell_t *sh = shell_create("/");
    if (!sh)
        panic("cannot allocate text shell");

    vga_text_clear();
    vga_text_write("TinOS text console\nType 'help' for commands or 'desktop' for graphics.\n\n");
    klog("[tty] text console ready\n");
    tty_prompt(sh);

    char line[TTY_LINE_MAX];
    size_t len = 0;
    for (;;) {
        audio_poll();
        bus_dispatch(16);
        async_run(8);
        thread_run(8);
        process_schedule();
        fs_io_poll();
        ktimer_poll(4);

        key_event_t key;
        while (keyboard_poll(&key)) {
            if ((key.mods & KMOD_CTRL) && (key.ch == 'c' || key.ch == 'C')) {
                vga_text_write("^C\n");
                process_cancel_input();
                len = 0;
                tty_prompt(sh);
            } else if (process_feed_key(&key)) {
                continue;
            } else if (key.code == KEY_BACKSPACE) {
                if (len) {
                    len--;
                    vga_text_putc('\b');
                }
            } else if (key.code == KEY_ENTER) {
                line[len] = '\0';
                vga_text_putc('\n');
                if (tty_execute(sh, line)) {
                    shell_destroy(sh);
                    klog("[tty] starting desktop\n");
                    return;
                }
                len = 0;
                tty_prompt(sh);
            } else if (key.ch >= ' ' && key.ch <= '~' &&
                       len + 1 < sizeof(line)) {
                line[len++] = key.ch;
                vga_text_putc(key.ch);
            }
        }

        arch_idle();
    }
}
