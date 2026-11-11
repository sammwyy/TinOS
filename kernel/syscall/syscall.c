#include <tinos/arch.h>
#include <tinos/console.h>
#include <tinos/input.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/process.h>
#include <tinos/syscall.h>
#include <uapi/tinos/syscall.h>

extern NORETURN void arch_leave_user(int status);

void syscall_dispatch(regs_t *frame)
{
    if ((frame->cs & 3) != 3 || !process_active()) {
        frame->eax = (uint32_t)-1;
        return;
    }

    switch (frame->eax) {
    case TINOS_SYS_EXIT:
        arch_leave_user((int)frame->ebx);
    case TINOS_SYS_READ: {
        if (frame->ebx != 0 || frame->edx != 1 ||
            !process_user_buffer(frame->ecx, 1, true)) {
            frame->eax = (uint32_t)-1;
            return;
        }
        key_event_t key;
        for (;;) {
            while (keyboard_poll(&key)) {
                if (key.code == KEY_ENTER)
                    key.ch = '\n';
                if (key.ch) {
                    *(char *)(uintptr_t)frame->ecx = key.ch;
                    cli();
                    frame->eax = 1;
                    return;
                }
            }
            arch_idle();
        }
    }
    case TINOS_SYS_WRITE:
        if ((frame->ebx != 1 && frame->ebx != 2) ||
            frame->edx > 1024 ||
            !process_user_buffer(frame->ecx, frame->edx, false)) {
            frame->eax = (uint32_t)-1;
            return;
        }
        for (size_t i = 0; i < frame->edx; i++) {
            char c = ((const char *)(uintptr_t)frame->ecx)[i];
            serial_putc(c);
            if (vga_text_active())
                vga_text_putc(c);
        }
        frame->eax = frame->edx;
        return;
    default:
        frame->eax = (uint32_t)-1;
        return;
    }
}
