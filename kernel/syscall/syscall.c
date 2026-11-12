#include <tinos/arch.h>
#include <tinos/console.h>
#include <tinos/fs.h>
#include <tinos/kernel.h>
#include <tinos/process.h>
#include <tinos/syscall.h>
#include <tinos/string.h>
#include <uapi/tinos/syscall.h>

extern NORETURN void arch_leave_user(int status);

static bool copy_user_path(uintptr_t address, size_t length,
                           char path[FS_PATH_MAX])
{
    if (!length || length >= FS_PATH_MAX ||
        !process_user_buffer(address, length, false))
        return false;
    memcpy(path, (const void *)address, length);
    for (size_t i = 0; i < length; i++)
        if (path[i] == '\0')
            return false;
    path[length] = '\0';
    return true;
}

static int spawn_user_process(const regs_t *frame)
{
    if (frame->edx == 0 || frame->edx > 16 ||
        !process_user_buffer(frame->esi, frame->edx * sizeof(uint32_t),
                             false))
        return -1;

    char path[FS_PATH_MAX];
    if (!copy_user_path(frame->ebx, frame->ecx, path) || path[0] != '/')
        return -1;

    uint32_t addresses[16];
    memcpy(addresses, (const void *)(uintptr_t)frame->esi,
           frame->edx * sizeof(uint32_t));
    char args[16][256];
    const char *argv[16];
    for (size_t i = 0; i < frame->edx; i++) {
        size_t j = 0;
        for (; j < sizeof(args[i]); j++) {
            uintptr_t address = addresses[i] + j;
            if (address < addresses[i] ||
                !process_user_buffer(address, 1, false))
                return -1;
            args[i][j] = *(const char *)address;
            if (!args[i][j])
                break;
        }
        if (j == sizeof(args[i]))
            return -1;
        argv[i] = args[i];
    }

    fs_node_t *file;
    if (fs_resolve(fs_root(), path, &file) != FS_OK ||
        file->type != FS_FILE || !file->data ||
        fs_size(file) > 256 * 1024)
        return -1;
    return process_spawn(file->data, fs_size(file), frame->edx, argv);
}

void syscall_dispatch(regs_t *frame)
{
    if ((frame->cs & 3) != 3 || !process_active()) {
        frame->eax = (uint32_t)-1;
        return;
    }

    switch (frame->eax) {
    case TINOS_SYS_EXIT:
        arch_leave_user((int)frame->ebx);
    case TINOS_SYS_GETPID:
        frame->eax = process_current_pid();
        return;
    case TINOS_SYS_SPAWN:
        frame->eax = (uint32_t)spawn_user_process(frame);
        return;
    case TINOS_SYS_WAIT:
        if (frame->ecx &&
            !process_user_buffer(frame->ecx, sizeof(int), true)) {
            frame->eax = (uint32_t)-1;
            return;
        }
        frame->eax = (uint32_t)process_wait(frame->ebx, frame->ecx, frame);
        return;
    case TINOS_SYS_OPEN: {
        if (frame->edx != 0) {
            frame->eax = (uint32_t)-1;
            return;
        }
        char path[FS_PATH_MAX];
        if (!copy_user_path(frame->ebx, frame->ecx, path)) {
            frame->eax = (uint32_t)-1;
            return;
        }
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
        int result = process_read_console((char *)(uintptr_t)frame->ecx);
        if (result == PROCESS_INPUT_WAIT)
            process_block_console(frame);
        frame->eax = (uint32_t)result;
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
