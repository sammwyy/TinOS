#include <tinos/arch.h>
#include <tinos/console.h>
#include <tinos/fs.h>
#include <tinos/kernel.h>
#include <tinos/process.h>
#include <tinos/syscall.h>
#include <tinos/string.h>
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
    case TINOS_SYS_OPEN: {
        if (frame->edx != 0 || frame->ecx == 0 ||
            frame->ecx >= FS_PATH_MAX ||
            !process_user_buffer(frame->ebx, frame->ecx, false)) {
            frame->eax = (uint32_t)-1;
            return;
        }
        char path[FS_PATH_MAX];
        memcpy(path, (const void *)(uintptr_t)frame->ebx, frame->ecx);
        for (size_t i = 0; i < frame->ecx; i++) {
            if (path[i] == '\0') {
                frame->eax = (uint32_t)-1;
                return;
            }
        }
        path[frame->ecx] = '\0';
        frame->eax = (uint32_t)process_file_open(path);
        return;
    }
    case TINOS_SYS_CLOSE:
        frame->eax = (uint32_t)process_file_close((int)frame->ebx);
        return;
    case TINOS_SYS_READ: {
        if (frame->edx > 1024 ||
            !process_user_buffer(frame->ecx, frame->edx, true)) {
            frame->eax = (uint32_t)-1;
            return;
        }
        if (frame->ebx != 0) {
            uint8_t buffer[1024];
            int count = process_file_read((int)frame->ebx, buffer, frame->edx);
            if (count > 0)
                memcpy((void *)(uintptr_t)frame->ecx, buffer, (size_t)count);
            frame->eax = (uint32_t)count;
            return;
        }
        if (frame->edx != 1) {
            frame->eax = (uint32_t)-1;
            return;
        }
        frame->eax = (uint32_t)process_read_console(
            (char *)(uintptr_t)frame->ecx);
        return;
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
