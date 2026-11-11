#include <tinos/arch.h>
#include <tinos/elf.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/lapic.h>
#include <tinos/process.h>
#include <tinos/smp.h>
#include <tinos/thread.h>
#include <tinos/user_vm.h>

extern int arch_enter_user(uintptr_t entry, uintptr_t stack);
extern NORETURN void arch_leave_user(int status);

static user_space_t *running;
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
        if ((running->table[index] & (writable ? 0x7u : 0x5u)) !=
            (writable ? 0x7u : 0x5u))
            return false;
    }
    return true;
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

bool process_run(const void *image, size_t length, int *status)
{
    if (!image || !status || !length || length > 256 * 1024 || running ||
        thread_current() ||
        lapic_id() != smp_bsp_apic_id())
        return false;

    user_space_t space = { 0 };
    if (!user_space_init(&space))
        return false;
    if (!user_space_map(&space, USER_STACK_TOP - 4096)) {
        user_space_destroy(&space);
        return false;
    }

    bool interrupts = irq_save();
    user_space_activate(&space);
    void *entry = NULL;
    bool loaded = elf_user_load(image, length, map_image_page, &space,
                                protect_image_page, &entry);
    if (loaded) {
        gdt_set_kernel_stack((uintptr_t)(kernel_stack + sizeof(kernel_stack)));
        running = &space;
        klog("[proc] entering ring 3 at 0x%08x\n", (uint32_t)(uintptr_t)entry);
        arch_user_fpu_disable();
        *status = arch_enter_user((uintptr_t)entry, USER_STACK_TOP);
        arch_user_fpu_enable();
        klog("[proc] ring 3 exit status %d\n", *status);
        running = NULL;
    }
    user_space_kernel();
    irq_restore(interrupts);
    user_space_destroy(&space);
    return loaded;
}
