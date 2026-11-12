#include <tinos/arch.h>
#include <tinos/dynlink.h>
#include <tinos/elf.h>
#include <tinos/fs.h>
#include <tinos/heap.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/lapic.h>
#include <tinos/mm.h>
#include <tinos/process.h>
#include <tinos/smp.h>
#include <tinos/string.h>
#include <tinos/thread.h>
#include <tinos/user_vm.h>
#include <tinos/user_window.h>
#include <tinos/user_audio.h>
#include <uapi/tinos/syscall.h>

extern int arch_enter_user(uintptr_t entry, uintptr_t stack);
extern int arch_resume_user(const regs_t *frame);
extern NORETURN void arch_leave_user(int status);

#define PROCESS_KERNEL_STACK_FRAMES 4
#define PROCESS_SLICE_TICKS 5
#define PROCESS_INPUT_SIZE 64

typedef struct {
    const uint8_t *data;
    uint8_t *owned;
    char *path;
    uint32_t flags;
    size_t size;
    size_t offset;
    bool open;
} process_file_t;

typedef struct {
    user_space_t space;
    process_file_t files[16];
    regs_t frame;
    void *kernel_stack;
    uintptr_t entry, user_sp;
    uint32_t pid;
    uint32_t parent_pid;
    dynlink_state_t libraries;
    uint32_t waiting_pid;
    uintptr_t waiting_status_address;
    uint32_t waited_pid;
    int waited_status;
    bool wait_ready;
    process_state_t state;
    int status;
    bool started;
    bool wants_input;
    bool blocked_output;
    bool blocked_audio;
    uint32_t audio_threshold;
    uintptr_t blocked_address;
    uintptr_t blocked_gui_address;
    uint64_t deadline;
    char input[PROCESS_INPUT_SIZE];
    unsigned input_head, input_tail;
} process_t;

static process_t *running;
static process_t processes[PROCESS_MAX];
static uint32_t next_pid = 1;
static size_t next_slot;

static bool pid_in_use(uint32_t pid)
{
    for (size_t i = 0; i < PROCESS_MAX; i++)
        if (processes[i].state != PROCESS_UNUSED && processes[i].pid == pid)
            return true;
    return false;
}

bool process_active(void)
{
    return running != NULL;
}

uint32_t process_current_pid(void)
{
    return running ? running->pid : 0;
}

int process_library_open(const char *name)
{
    if (!running)
        return -1;
    return dynlink_open(name, &running->space, &running->libraries);
}

int process_library_close(int handle)
{
    if (!running)
        return -1;
    return dynlink_close(handle, &running->space, &running->libraries);
}

uintptr_t process_library_symbol(int handle, const char *name)
{
    if (!running)
        return 0;
    return dynlink_symbol(handle, name, &running->space, &running->libraries);
}

uintptr_t process_library_initializer(void)
{
    return running ? dynlink_next_initializer(&running->libraries) : 0;
}

bool process_user_buffer(uintptr_t address, size_t length, bool writable)
{
    if (!running || address < USER_BASE || address >= USER_REGION_END ||
        length > USER_REGION_END - address)
        return false;
    uintptr_t end = address + length;
    for (uintptr_t page = address & ~0xfffu; page < end; page += 4096) {
        size_t index = (page - USER_BASE) / 4096;
        if ((running->space.table[index] & (writable ? 0x7u : 0x5u)) !=
            (writable ? 0x7u : 0x5u))
            return false;
    }
    return true;
}

bool process_wants_input(void)
{
    for (size_t i = 0; i < PROCESS_MAX; i++)
        if ((processes[i].state == PROCESS_READY ||
             processes[i].state == PROCESS_BLOCKED) &&
            processes[i].wants_input && !processes[i].waiting_pid &&
            !processes[i].wait_ready)
            return true;
    return false;
}

bool process_feed_key(const key_event_t *key)
{
    for (size_t i = 0; i < PROCESS_MAX; i++) {
        process_t *p = &processes[i];
        if ((p->state != PROCESS_READY && p->state != PROCESS_BLOCKED) ||
            !p->wants_input || p->waiting_pid || p->wait_ready)
            continue;
        char ch = key->code == KEY_ENTER ? '\n' : key->ch;
        if (ch) {
            unsigned next = (p->input_tail + 1) % PROCESS_INPUT_SIZE;
            if (next != p->input_head) {
                p->input[p->input_tail] = ch;
                p->input_tail = next;
            }
            if (p->state == PROCESS_BLOCKED && p->blocked_address &&
                p->input_head != p->input_tail)
                p->state = PROCESS_READY;
        }
        return true;
    }
    return false;
}

int process_read_console(char *ch)
{
    if (!running)
        return -1;
    running->wants_input = true;
    if (running->input_head == running->input_tail)
        return PROCESS_INPUT_WAIT;
    *ch = running->input[running->input_head];
    running->input_head = (running->input_head + 1) % PROCESS_INPUT_SIZE;
    return 1;
}

NORETURN void process_block_console(regs_t *frame)
{
    KASSERT(running != NULL && (frame->cs & 3) == 3);
    memcpy(&running->frame, frame, sizeof(*frame));
    running->blocked_address = frame->ecx;
    running->state = PROCESS_BLOCKED;
    arch_leave_user(0);
}

NORETURN void process_block_console_output(regs_t *frame)
{
    KASSERT(running != NULL && (frame->cs & 3) == 3);
    memcpy(&running->frame, frame, sizeof(*frame));
    running->blocked_output = true;
    running->state = PROCESS_BLOCKED;
    arch_leave_user(0);
}

NORETURN void process_block_audio(regs_t *frame, uint32_t maximum)
{
    KASSERT(running != NULL && (frame->cs & 3) == 3);
    memcpy(&running->frame, frame, sizeof(*frame));
    running->audio_threshold = maximum;
    running->blocked_audio = true;
    running->state = PROCESS_BLOCKED;
    klog("[proc] pid %u waiting for PCM queue\n", running->pid);
    arch_leave_user(0);
}

void process_audio_wake(void)
{
    for (size_t i = 0; i < PROCESS_MAX; i++) {
        process_t *p = &processes[i];
        if (p->state != PROCESS_BLOCKED || !p->blocked_audio)
            continue;
        int ready = user_audio_wait_ready(p->pid, 1, p->audio_threshold);
        if (ready == 0)
            continue;
        p->frame.eax = ready < 0 ? (uint32_t)-1 : 0;
        p->blocked_audio = false;
        p->state = PROCESS_READY;
    }
}

void process_wake_console_output(uint32_t parent_pid)
{
    for (size_t i = 0; i < PROCESS_MAX; i++)
        if (processes[i].parent_pid == parent_pid &&
            processes[i].state == PROCESS_BLOCKED &&
            processes[i].blocked_output)
            processes[i].state = PROCESS_READY;
}

int process_console_output(const void *data, size_t length)
{
    if (!running || !running->parent_pid)
        return -1;
    return desktop_user_window_console_write(running->parent_pid, data, length);
}

int process_console_send(uint32_t child_pid, uint8_t ch)
{
    if (!running || !child_pid || !ch)
        return -1;
    for (size_t i = 0; i < PROCESS_MAX; i++) {
        process_t *child = &processes[i];
        if (child->pid != child_pid || child->parent_pid != running->pid ||
            (child->state != PROCESS_READY &&
             child->state != PROCESS_BLOCKED))
            continue;
        unsigned next = (child->input_tail + 1) % PROCESS_INPUT_SIZE;
        if (next == child->input_head)
            return -1;
        child->input[child->input_tail] = ch;
        child->input_tail = next;
        if (child->state == PROCESS_BLOCKED && child->blocked_address)
            child->state = PROCESS_READY;
        return 0;
    }
    return -1;
}

int process_kill_child(uint32_t child_pid)
{
    if (!running || !child_pid)
        return -1;
    for (size_t i = 0; i < PROCESS_MAX; i++)
        if (processes[i].pid == child_pid &&
            processes[i].parent_pid == running->pid)
            return process_kill(child_pid) ? 0 : -1;
    return -1;
}

NORETURN void process_block_gui(regs_t *frame, uintptr_t event_address)
{
    KASSERT(running != NULL && (frame->cs & 3) == 3);
    memcpy(&running->frame, frame, sizeof(*frame));
    running->blocked_gui_address = event_address;
    running->state = PROCESS_BLOCKED;
    arch_leave_user(0);
}

void process_gui_wake(uint32_t pid)
{
    for (size_t i = 0; i < PROCESS_MAX; i++)
        if (processes[i].pid == pid && processes[i].state == PROCESS_BLOCKED &&
            processes[i].blocked_gui_address) {
            processes[i].state = PROCESS_READY;
            return;
        }
}

size_t process_list(process_info_t *out, size_t capacity)
{
    size_t count = 0;
    for (size_t i = 0; i < PROCESS_MAX; i++) {
        process_t *p = &processes[i];
        if (p->state == PROCESS_UNUSED)
            continue;
        if (count < capacity)
            out[count] = (process_info_t){
                p->pid, p->parent_pid, p->state, p->status
            };
        count++;
    }
    return count;
}

static bool writable_path(const char *path)
{
    if (!path || path[0] != '/')
        return false;
    fs_node_t *apps, *node;
    if (fs_resolve(fs_root(), "/apps", &apps) != FS_OK)
        return false;
    fs_result_t result = fs_resolve(fs_root(), path, &node);
    if (result == FS_ERR_NOENT) {
        char name[FS_NAME_MAX];
        if (fs_resolve_parent(fs_root(), path, &node, name, sizeof(name)) != FS_OK)
            return false;
    } else if (result != FS_OK) {
        return false;
    }
    return !fs_is_ancestor(apps, node);
}

int process_file_open(const char *path, uint32_t flags)
{
    bool writable = (flags & 3u) != TINOS_O_RDONLY;
    if (!running || path[0] != '/' ||
        (flags & ~(TINOS_O_WRONLY | TINOS_O_RDWR | TINOS_O_CREAT |
                   TINOS_O_TRUNC | TINOS_O_APPEND)) ||
        (flags & 3u) == 3u ||
        (!writable && (flags & (TINOS_O_CREAT | TINOS_O_TRUNC |
                                TINOS_O_APPEND))) ||
        (writable && !writable_path(path)))
        return -1;
    int available = -1;
    for (int fd = 3; fd < (int)ARRAY_SIZE(running->files); fd++) {
        if (!running->files[fd].open) {
            available = fd;
            break;
        }
    }
    if (available < 0)
        return -1;
    fs_node_t *node;
    fs_result_t result = fs_resolve(fs_root(), path, &node);
    if (result == FS_ERR_NOENT && (flags & TINOS_O_CREAT))
        result = fs_create(fs_root(), path, &node);
    if (result != FS_OK || node->type != FS_FILE || node->backend ||
        (!node->data && node->size))
        return -1;
    process_file_t *file = &running->files[available];
    if (writable) {
        char *copy = kstrdup(path);
        if (!copy)
            return -1;
        if ((flags & TINOS_O_TRUNC) && fs_write(node, NULL, 0) != FS_OK) {
            kfree(copy);
            return -1;
        }
        *file = (process_file_t){
            .path = copy, .flags = flags, .offset =
                (flags & TINOS_O_APPEND) ? node->size : 0, .open = true,
        };
    } else {
        uint8_t *copy = NULL;
        if (!node->data_borrowed && node->size) {
            copy = kmalloc(node->size);
            if (!copy)
                return -1;
            memcpy(copy, node->data, node->size);
        }
        *file = (process_file_t){
            .data = copy ? copy : node->data,
            .owned = copy,
            .size = node->size,
            .open = true,
        };
    }
    return available;
}

int process_file_read(int fd, void *buffer, size_t length)
{
    if (!running || fd < 3 || fd >= (int)ARRAY_SIZE(running->files) ||
        !running->files[fd].open)
        return -1;
    process_file_t *file = &running->files[fd];
    fs_node_t *node = NULL;
    size_t size = file->size;
    if (file->path) {
        if ((file->flags & 3u) == TINOS_O_WRONLY ||
            fs_resolve(fs_root(), file->path, &node) != FS_OK ||
            node->type != FS_FILE || node->backend)
            return -1;
        size = node->size;
    }
    if (file->offset >= size)
        return 0;
    size_t remain = size - file->offset;
    if (length > remain)
        length = remain;
    if (node) {
        if (!fs_read_at(node, file->offset, buffer, length))
            return -1;
    } else {
        memcpy(buffer, file->data + file->offset, length);
    }
    file->offset += length;
    return (int)length;
}

int process_file_write(int fd, const void *buffer, size_t length)
{
    if (!running || fd < 3 || fd >= (int)ARRAY_SIZE(running->files) ||
        !running->files[fd].open || !running->files[fd].path)
        return -1;
    process_file_t *file = &running->files[fd];
    fs_node_t *node;
    if (fs_resolve(fs_root(), file->path, &node) != FS_OK ||
        node->type != FS_FILE || node->backend)
        return -1;
    size_t offset = (file->flags & TINOS_O_APPEND) ? node->size : file->offset;
    if (offset > 64 * 1024 || length > 64 * 1024 - offset ||
        fs_write_at(node, offset, buffer, length) != FS_OK)
        return -1;
    file->offset = offset + length;
    return (int)length;
}

int process_file_mkdir(const char *path)
{
    if (!running || !writable_path(path))
        return -1;
    return fs_mkdir(fs_root(), path, NULL) == FS_OK ? 0 : -1;
}

int process_file_unlink(const char *path)
{
    if (!running || !writable_path(path))
        return -1;
    return fs_remove(fs_root(), path, false) == FS_OK ? 0 : -1;
}

int process_file_close(int fd)
{
    if (!running || fd < 3 || fd >= (int)ARRAY_SIZE(running->files) ||
        !running->files[fd].open)
        return -1;
    kfree(running->files[fd].owned);
    kfree(running->files[fd].path);
    memset(&running->files[fd], 0, sizeof(running->files[fd]));
    return 0;
}

NORETURN void process_abort(int reason)
{
    klog("[proc] user exception %d\n", reason);
    arch_leave_user(128 + reason);
}

static bool map_image_page(void *context, uintptr_t page)
{
    return user_space_map(context, page);
}

static void protect_image_page(void *context, uintptr_t page)
{
    user_space_readonly(context, page);
}

static bool prepare_user_stack(int argc, const char *const argv[],
                               uintptr_t *user_sp)
{
    uintptr_t top = USER_STACK_TOP;
    uintptr_t pointers[16];
    for (int i = argc - 1; i >= 0; i--) {
        if (!argv[i])
            return false;
        size_t length = strlen(argv[i]);
        if (length > 255 ||
            top < USER_STACK_TOP - 4096 + length + 1 + 128)
            return false;
        top -= length + 1;
        memcpy((void *)top, argv[i], length + 1);
        pointers[i] = top;
    }
    top &= ~(uintptr_t)3;
    if (top - (size_t)(argc + 2) * sizeof(uint32_t) <
        USER_STACK_TOP - 4096 + 128)
        return false;
    top -= (size_t)(argc + 2) * sizeof(uint32_t);
    uint32_t *frame = (uint32_t *)top;
    frame[0] = (uint32_t)argc;
    for (int i = 0; i < argc; i++)
        frame[i + 1] = (uint32_t)pointers[i];
    frame[argc + 1] = 0;
    *user_sp = top;
    return true;
}

static void release_process(process_t *p)
{
    user_audio_release(p->pid);
    desktop_user_window_release(p->pid);
    for (size_t i = 3; i < ARRAY_SIZE(p->files); i++)
        kfree(p->files[i].owned);
    for (size_t i = 3; i < ARRAY_SIZE(p->files); i++)
        kfree(p->files[i].path);
    user_space_destroy(&p->space);
    pmm_free_contig(p->kernel_stack, PROCESS_KERNEL_STACK_FRAMES);
    p->kernel_stack = NULL;
}

static void complete_process(process_t *p)
{
    for (size_t i = 0; i < PROCESS_MAX; i++)
        if (processes[i].parent_pid == p->pid)
            processes[i].parent_pid = 0;

    for (size_t i = 0; i < PROCESS_MAX; i++) {
        process_t *parent = &processes[i];
        if (parent->pid != p->parent_pid ||
            parent->state != PROCESS_BLOCKED ||
            parent->waiting_pid != p->pid)
            continue;
        parent->waited_pid = p->pid;
        parent->waited_status = p->status;
        parent->waiting_pid = 0;
        parent->wait_ready = true;
        parent->state = PROCESS_READY;
        memset(p, 0, sizeof(*p));
        return;
    }
    desktop_user_window_child_exit(p->parent_pid, p->pid, p->status);
}

int process_spawn(const void *image, size_t length, int argc,
                  const char *const argv[])
{
    if (!image || !length || length > 256 * 1024 ||
        argc < 1 || argc > 16 || !argv || thread_current() ||
        lapic_id() != smp_bsp_apic_id())
        return -1;

    process_t *parent = running;
    process_t *p = NULL;
    for (size_t i = 0; i < PROCESS_MAX; i++)
        if (processes[i].state == PROCESS_UNUSED) {
            p = &processes[i];
            break;
        }
    if (!p)
        for (size_t i = 0; i < PROCESS_MAX; i++)
            if (processes[i].state == PROCESS_EXITED &&
                processes[i].parent_pid == 0) {
                p = &processes[i];
                break;
            }
    if (!p)
        return -1;
    memset(p, 0, sizeof(*p));

    p->kernel_stack = pmm_alloc_contig(PROCESS_KERNEL_STACK_FRAMES);
    if (!p->kernel_stack)
        return -1;
    if (!user_space_init(&p->space) ||
        !user_space_map(&p->space, USER_STACK_TOP - 4096)) {
        release_process(p);
        return -1;
    }

    bool interrupts = irq_save();
    user_space_activate(&p->space);
    void *entry = NULL;
    bool loaded = elf_user_load(image, length, map_image_page, &p->space,
                                protect_image_page, &entry);
    if (loaded)
        loaded = dynlink_load(image, length, &p->space, &p->libraries);
    if (loaded)
        loaded = prepare_user_stack(argc, argv, &p->user_sp);
    if (parent)
        user_space_activate(&parent->space);
    else
        user_space_kernel();
    irq_restore(interrupts);
    if (!loaded) {
        release_process(p);
        return -1;
    }
    p->entry = (uintptr_t)entry;
    p->parent_pid = parent ? parent->pid : 0;
    do {
        p->pid = next_pid;
        next_pid = next_pid == INT32_MAX ? 1 : next_pid + 1;
    } while (pid_in_use(p->pid));
    p->state = PROCESS_READY;
    klog("[proc] spawned pid %u\n", p->pid);
    return (int)p->pid;
}

int process_wait(uint32_t pid, uintptr_t status_address, regs_t *frame)
{
    if (!running || !pid)
        return -1;
    for (size_t i = 0; i < PROCESS_MAX; i++) {
        process_t *child = &processes[i];
        if (child->state == PROCESS_UNUSED || child->pid != pid ||
            child->parent_pid != running->pid)
            continue;
        if (child->state == PROCESS_EXITED) {
            if (status_address)
                *(int *)status_address = child->status;
            memset(child, 0, sizeof(*child));
            return (int)pid;
        }
        running->waiting_pid = pid;
        running->waiting_status_address = status_address;
        memcpy(&running->frame, frame, sizeof(*frame));
        running->state = PROCESS_BLOCKED;
        arch_leave_user(0);
    }
    return -1;
}

void process_maybe_preempt(regs_t *frame)
{
    if (!running || (frame->cs & 3) != 3 ||
        timer_ticks() < running->deadline)
        return;
    memcpy(&running->frame, frame, sizeof(*frame));
    running->state = PROCESS_READY;
    arch_leave_user(0);
}

void process_on_tick(regs_t *frame)
{
    process_maybe_preempt(frame);
}

void process_schedule(void)
{
    if (running || thread_current() || lapic_id() != smp_bsp_apic_id())
        return;
    process_t *p = NULL;
    for (size_t i = 0; i < PROCESS_MAX; i++) {
        next_slot = (next_slot + 1) % PROCESS_MAX;
        if (processes[next_slot].state == PROCESS_READY) {
            p = &processes[next_slot];
            break;
        }
    }
    if (!p)
        return;

    bool interrupts = irq_save();
    running = p;
    p->state = PROCESS_RUNNING;
    p->deadline = timer_ticks() + PROCESS_SLICE_TICKS;
    user_space_activate(&p->space);
    if (p->blocked_output) {
        p->frame.eax = 0;
        p->blocked_output = false;
    }
    if (p->blocked_gui_address) {
        tinos_gui_event_t event;
        KASSERT(desktop_user_window_next(p->pid, 1, &event) == 1);
        memcpy((void *)p->blocked_gui_address, &event, sizeof(event));
        p->frame.eax = 1;
        p->blocked_gui_address = 0;
    }
    if (p->blocked_address) {
        KASSERT(p->input_head != p->input_tail);
        *(char *)p->blocked_address = p->input[p->input_head];
        p->input_head = (p->input_head + 1) % PROCESS_INPUT_SIZE;
        p->frame.eax = 1;
        p->blocked_address = 0;
    }
    if (p->wait_ready) {
        if (p->waiting_status_address)
            *(int *)p->waiting_status_address = p->waited_status;
        p->frame.eax = p->waited_pid;
        p->waiting_status_address = 0;
        p->wait_ready = false;
    }
    gdt_set_kernel_stack((uintptr_t)p->kernel_stack +
                         PROCESS_KERNEL_STACK_FRAMES * 4096);
    arch_user_fpu_disable();
    int status;
    if (!p->started) {
        p->started = true;
        klog("[proc] entering ring 3 at 0x%08x\n", (uint32_t)p->entry);
        status = arch_enter_user(p->entry, p->user_sp);
    } else {
        status = arch_resume_user(&p->frame);
    }
    arch_user_fpu_enable();
    user_space_kernel();
    running = NULL;
    if (p->state == PROCESS_RUNNING) {
        p->status = status;
        p->state = PROCESS_EXITED;
        klog("[proc] ring 3 exit status %d\n", status);
        release_process(p);
        complete_process(p);
    }
    irq_restore(interrupts);
}

bool process_kill(uint32_t pid)
{
    for (size_t i = 0; i < PROCESS_MAX; i++) {
        process_t *p = &processes[i];
        if (p->pid != pid ||
            (p->state != PROCESS_READY && p->state != PROCESS_BLOCKED))
            continue;
        p->status = 130;
        p->state = PROCESS_EXITED;
        release_process(p);
        klog("[proc] killed pid %u\n", pid);
        complete_process(p);
        return true;
    }
    return false;
}

bool process_cancel_input(void)
{
    for (size_t i = 0; i < PROCESS_MAX; i++)
        if ((processes[i].state == PROCESS_READY ||
             processes[i].state == PROCESS_BLOCKED) &&
            processes[i].wants_input && !processes[i].waiting_pid &&
            !processes[i].wait_ready)
            return process_kill(processes[i].pid);
    return false;
}

bool process_reap(uint32_t pid, int *status)
{
    for (size_t i = 0; i < PROCESS_MAX; i++) {
        process_t *p = &processes[i];
        if (p->pid != pid || p->state != PROCESS_EXITED ||
            p->parent_pid != 0)
            continue;
        if (status)
            *status = p->status;
        memset(p, 0, sizeof(*p));
        return true;
    }
    return false;
}
