#include <tinos/arch.h>
#include <tinos/elf.h>
#include <tinos/fs.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/lapic.h>
#include <tinos/process.h>
#include <tinos/smp.h>
#include <tinos/string.h>
#include <tinos/thread.h>
#include <tinos/user_vm.h>

extern int arch_enter_user(uintptr_t entry, uintptr_t stack);
extern NORETURN void arch_leave_user(int status);

typedef struct {
    fs_node_t *node;
    size_t offset;
} process_file_t;

typedef struct {
    user_space_t space;
    process_file_t files[16];
} process_t;

static process_t *running;
static uint8_t kernel_stack[16384] __attribute__((aligned(16)));

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

int process_file_open(const char *path)
{
    if (!running || path[0] != '/')
        return -1;
    fs_node_t *node;
    if (fs_resolve(fs_root(), path, &node) != FS_OK ||
        node->type != FS_FILE || !node->data)
        return -1;
    for (int fd = 3; fd < (int)ARRAY_SIZE(running->files); fd++) {
        if (!running->files[fd].node) {
            running->files[fd].node = node;
            running->files[fd].offset = 0;
            return fd;
        }
    }
    return -1;
}

int process_file_read(int fd, void *buffer, size_t length)
{
    if (!running || fd < 3 || fd >= (int)ARRAY_SIZE(running->files) ||
        !running->files[fd].node)
        return -1;
    process_file_t *file = &running->files[fd];
    size_t size = fs_size(file->node);
    if (file->offset >= size)
        return 0;
    size_t remain = size - file->offset;
    if (length > remain)
        length = remain;
    if (!fs_read_at(file->node, file->offset, buffer, length))
        return -1;
    file->offset += length;
    return (int)length;
}

int process_file_close(int fd)
{
    if (!running || fd < 3 || fd >= (int)ARRAY_SIZE(running->files) ||
        !running->files[fd].node)
        return -1;
    running->files[fd].node = NULL;
    running->files[fd].offset = 0;
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

bool process_run(const void *image, size_t length, int argc,
                 const char *const argv[], int *status)
{
    if (!image || !status || !length || length > 256 * 1024 || running ||
        argc < 1 || argc > 16 || !argv || thread_current() ||
        lapic_id() != smp_bsp_apic_id())
        return false;

    process_t process = { 0 };
    if (!user_space_init(&process.space))
        return false;
    if (!user_space_map(&process.space, USER_STACK_TOP - 4096)) {
        user_space_destroy(&process.space);
        return false;
    }

    bool interrupts = irq_save();
    user_space_activate(&process.space);
    void *entry = NULL;
    bool loaded = elf_user_load(image, length, map_image_page, &process.space,
                                protect_image_page, &entry);
    uintptr_t user_sp;
    if (loaded)
        loaded = prepare_user_stack(argc, argv, &user_sp);
    if (loaded) {
        gdt_set_kernel_stack((uintptr_t)(kernel_stack + sizeof(kernel_stack)));
        running = &process;
        klog("[proc] entering ring 3 at 0x%08x\n", (uint32_t)(uintptr_t)entry);
        arch_user_fpu_disable();
        *status = arch_enter_user((uintptr_t)entry, user_sp);
        arch_user_fpu_enable();
        klog("[proc] ring 3 exit status %d\n", *status);
        running = NULL;
    }
    user_space_kernel();
    irq_restore(interrupts);
    user_space_destroy(&process.space);
    return loaded;
}
