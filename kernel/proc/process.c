#include <tinos/arch.h>
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
    process_state_t state;
    int status;
    bool started;
    bool wants_input;
    uint64_t deadline;
    char input[PROCESS_INPUT_SIZE];
    unsigned input_head, input_tail;
} process_t;

static process_t *running;
static process_t processes[PROCESS_MAX];
static uint32_t next_pid = 1;
static size_t next_slot;

bool process_active(void)
{
    return running != NULL;
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
        if (processes[i].state == PROCESS_READY &&
            processes[i].wants_input)
            return true;
    return false;
}

bool process_feed_key(const key_event_t *key)
{
    for (size_t i = 0; i < PROCESS_MAX; i++) {
        process_t *p = &processes[i];
        if (p->state != PROCESS_READY || !p->wants_input)
            continue;
        char ch = key->code == KEY_ENTER ? '\n' : key->ch;
        if (ch) {
            unsigned next = (p->input_tail + 1) % PROCESS_INPUT_SIZE;
            if (next != p->input_head) {
                p->input[p->input_tail] = ch;
                p->input_tail = next;
            }
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
        return TINOS_ERR_AGAIN;
    *ch = running->input[running->input_head];
    running->input_head = (running->input_head + 1) % PROCESS_INPUT_SIZE;
    return 1;
}

size_t process_list(process_info_t *out, size_t capacity)
{
    size_t count = 0;
    for (size_t i = 0; i < PROCESS_MAX; i++) {
        process_t *p = &processes[i];
        if (p->state == PROCESS_UNUSED)
            continue;
        if (count < capacity)
            out[count] = (process_info_t){ p->pid, p->state, p->status };
        count++;
    }
    return count;
}

int process_file_open(const char *path)
{
    if (!running || path[0] != '/')
        return -1;
    fs_node_t *node;
    if (fs_resolve(fs_root(), path, &node) != FS_OK ||
        node->type != FS_FILE || (!node->data && node->size))
        return -1;
    for (int fd = 3; fd < (int)ARRAY_SIZE(running->files); fd++) {
        process_file_t *file = &running->files[fd];
        if (file->open)
            continue;
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
        return fd;
    }
    return -1;
}

int process_file_read(int fd, void *buffer, size_t length)
{
    if (!running || fd < 3 || fd >= (int)ARRAY_SIZE(running->files) ||
        !running->files[fd].open)
        return -1;
    process_file_t *file = &running->files[fd];
    if (file->offset >= file->size)
        return 0;
    size_t remain = file->size - file->offset;
    if (length > remain)
        length = remain;
    memcpy(buffer, file->data + file->offset, length);
    file->offset += length;
    return (int)length;
}

int process_file_close(int fd)
{
    if (!running || fd < 3 || fd >= (int)ARRAY_SIZE(running->files) ||
        !running->files[fd].open)
        return -1;
    kfree(running->files[fd].owned);
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
    for (size_t i = 3; i < ARRAY_SIZE(p->files); i++)
        kfree(p->files[i].owned);
    user_space_destroy(&p->space);
    pmm_free_contig(p->kernel_stack, PROCESS_KERNEL_STACK_FRAMES);
    p->kernel_stack = NULL;
}

int process_spawn(const void *image, size_t length, int argc,
                  const char *const argv[])
{
    if (!image || !length || length > 256 * 1024 || running ||
        argc < 1 || argc > 16 || !argv || thread_current() ||
        lapic_id() != smp_bsp_apic_id())
        return -1;

    process_t *p = NULL;
    for (size_t i = 0; i < PROCESS_MAX; i++)
        if (processes[i].state == PROCESS_UNUSED) {
            p = &processes[i];
            break;
        }
    if (!p)
        for (size_t i = 0; i < PROCESS_MAX; i++)
            if (processes[i].state == PROCESS_EXITED) {
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
        loaded = prepare_user_stack(argc, argv, &p->user_sp);
    user_space_kernel();
    irq_restore(interrupts);
    if (!loaded) {
        release_process(p);
        return -1;
    }
    p->entry = (uintptr_t)entry;
    p->pid = next_pid++;
    if (!next_pid)
        next_pid = 1;
    p->state = PROCESS_READY;
    klog("[proc] spawned pid %u\n", p->pid);
    return (int)p->pid;
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
    }
    irq_restore(interrupts);
}

bool process_kill(uint32_t pid)
{
    for (size_t i = 0; i < PROCESS_MAX; i++) {
        process_t *p = &processes[i];
        if (p->pid != pid || p->state != PROCESS_READY)
            continue;
        p->status = 130;
        p->state = PROCESS_EXITED;
        release_process(p);
        klog("[proc] killed pid %u\n", pid);
        return true;
    }
    return false;
}

bool process_cancel_input(void)
{
    for (size_t i = 0; i < PROCESS_MAX; i++)
        if (processes[i].state == PROCESS_READY &&
            processes[i].wants_input)
            return process_kill(processes[i].pid);
    return false;
}

bool process_reap(uint32_t pid, int *status)
{
    for (size_t i = 0; i < PROCESS_MAX; i++) {
        process_t *p = &processes[i];
        if (p->pid != pid || p->state != PROCESS_EXITED)
            continue;
        if (status)
            *status = p->status;
        memset(p, 0, sizeof(*p));
        return true;
    }
    return false;
}
