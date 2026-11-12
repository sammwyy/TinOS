#include <tinos/arch.h>
#include <tinos/console.h>
#include <tinos/fs.h>
#include <tinos/kernel.h>
#include <tinos/input.h>
#include <tinos/process.h>
#include <tinos/sh.h>
#include <tinos/syscall.h>
#include <tinos/string.h>
#include <tinos/user_window.h>
#include <uapi/tinos/desktop.h>
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

static int stat_user_path(const regs_t *frame, bool directory_entry)
{
    char path[FS_PATH_MAX];
    uintptr_t output = directory_entry ? frame->esi : frame->edx;
    if (!copy_user_path(frame->ebx, frame->ecx, path) ||
        !process_user_buffer(output, sizeof(tinos_dirent_t), true) ||
        (directory_entry && frame->edx > 4096))
        return -1;
    fs_node_t *node;
    if (fs_resolve(fs_root(), path, &node) != FS_OK)
        return -1;
    if (directory_entry) {
        if (node->type != FS_DIR)
            return -1;
        node = node->children;
        for (uint32_t i = 0; i < frame->edx && node; i++)
            node = node->next;
        if (!node)
            return 0;
    }
    tinos_dirent_t entry = {
        .type = node->type == FS_DIR ? TINOS_DIRENT_DIR : TINOS_DIRENT_FILE,
        .size = fs_size(node),
    };
    strlcpy(entry.name, node->name, sizeof(entry.name));
    memcpy((void *)output, &entry, sizeof(entry));
    return 1;
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
    case TINOS_SYS_READDIR:
        frame->eax = (uint32_t)stat_user_path(frame, true);
        return;
    case TINOS_SYS_STAT:
        frame->eax = (uint32_t)stat_user_path(frame, false);
        return;
    case TINOS_SYS_SYSINFO: {
        if (!process_user_buffer(frame->ebx, sizeof(tinos_system_info_t),
                                 true)) {
            frame->eax = (uint32_t)-1;
            return;
        }
        sys_info_t current;
        sys_info_get(&current);
        tinos_system_info_t info = {
            .uptime_ms = current.uptime_ms,
            .memory_total_kib = (uint32_t)(current.mem_total / 1024),
            .memory_free_kib = (uint32_t)(current.mem_free / 1024),
            .screen_width = current.screen_width,
            .screen_height = current.screen_height,
            .fs_nodes = fs_node_count(),
        };
        strlcpy(info.cpu, current.cpu, sizeof(info.cpu));
        strlcpy(info.platform, current.platform, sizeof(info.platform));
        strlcpy(info.clock, current.datetime, sizeof(info.clock));
        memcpy((void *)(uintptr_t)frame->ebx, &info, sizeof(info));
        frame->eax = 0;
        return;
    }
    case TINOS_SYS_IOCTL:
        if (frame->ebx == 0 && frame->ecx == TINOS_IOCTL_KBD_GET_LAYOUT) {
            frame->eax = keyboard_get_layout();
        } else if (frame->ebx == 0 &&
                   frame->ecx == TINOS_IOCTL_KBD_SET_LAYOUT &&
                   frame->edx <= TINOS_KBD_ES) {
            keyboard_set_layout(frame->edx == TINOS_KBD_US ?
                                KEYBOARD_LAYOUT_US : KEYBOARD_LAYOUT_ES);
            frame->eax = 0;
        } else {
            frame->eax = (uint32_t)-1;
        }
        return;
    case TINOS_SYS_MKDIR:
    case TINOS_SYS_UNLINK: {
        char path[FS_PATH_MAX];
        if (!copy_user_path(frame->ebx, frame->ecx, path)) {
            frame->eax = (uint32_t)-1;
            return;
        }
        frame->eax = (uint32_t)(frame->eax == TINOS_SYS_MKDIR ?
                                 process_file_mkdir(path) :
                                 process_file_unlink(path));
        return;
    }
    case TINOS_SYS_GUI_OPEN: {
        if (!frame->ecx || frame->ecx >= 64 ||
            !process_user_buffer(frame->ebx, frame->ecx, false)) {
            frame->eax = (uint32_t)-1;
            return;
        }
        char title[64];
        memcpy(title, (const void *)(uintptr_t)frame->ebx, frame->ecx);
        title[frame->ecx] = '\0';
        frame->eax = (uint32_t)desktop_user_window_open(process_current_pid(),
                         title, (int)frame->edx, (int)frame->esi);
        return;
    }
    case TINOS_SYS_GUI_PRESENT:
        if (!frame->edx || frame->edx > TINOS_GUI_TEXT_SIZE ||
            !process_user_buffer(frame->ecx, frame->edx, false)) {
            frame->eax = (uint32_t)-1;
            return;
        }
        frame->eax = (uint32_t)desktop_user_window_present(process_current_pid(),
                            frame->ebx, (const char *)(uintptr_t)frame->ecx,
                            frame->edx, frame->esi);
        return;
    case TINOS_SYS_GUI_EVENT: {
        if (!process_user_buffer(frame->ecx, sizeof(tinos_gui_event_t), true)) {
            frame->eax = (uint32_t)-1;
            return;
        }
        tinos_gui_event_t event;
        int result = desktop_user_window_next(process_current_pid(), frame->ebx,
                                               &event);
        if (result == 0)
            process_block_gui(frame, frame->ecx);
        if (result > 0)
            memcpy((void *)(uintptr_t)frame->ecx, &event, sizeof(event));
        frame->eax = (uint32_t)result;
        return;
    }
    case TINOS_SYS_GUI_CLOSE:
        frame->eax = (uint32_t)desktop_user_window_close(process_current_pid(),
                                                          frame->ebx);
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
        char path[FS_PATH_MAX];
        if (!copy_user_path(frame->ebx, frame->ecx, path)) {
            frame->eax = (uint32_t)-1;
            return;
        }
        frame->eax = (uint32_t)process_file_open(path, frame->edx);
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
        if (frame->edx > 1024 ||
            !process_user_buffer(frame->ecx, frame->edx, false)) {
            frame->eax = (uint32_t)-1;
            return;
        }
        if (frame->ebx != 1 && frame->ebx != 2) {
            frame->eax = (uint32_t)process_file_write(
                (int)frame->ebx, (const void *)(uintptr_t)frame->ecx, frame->edx);
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
