/* TinOS kernel entry point and boot sequence.
 *
 * kmain() runs the text-mode stage: it brings up the descriptor tables,
 * interrupts, memory, input, the filesystem and finally the graphics mode,
 * reporting each step on screen and on the serial port.  Once the display
 * has been switched to a linear framebuffer the desktop takes over and
 * never returns.
 */

#include <tinos/arch.h>
#include <tinos/async.h>
#include <tinos/audio.h>
#include <tinos/bus.h>
#include <tinos/codec.h>
#include <tinos/ata.h>
#include <tinos/storage.h>
#include <tinos/uhci.h>
#include <tinos/virtio_blk.h>
#include <tinos/console.h>
#include <tinos/desktop.h>
#include <tinos/driver.h>
#include <tinos/e1000.h>
#include <tinos/fs.h>
#include <tinos/gfx.h>
#include <tinos/heap.h>
#include <tinos/image.h>
#include <tinos/input.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/mm.h>
#include <tinos/net.h>
#include <tinos/boot_info.h>
#include <tinos/pci.h>
#include <tinos/printf.h>
#include <tinos/runloop.h>
#include <tinos/selftest.h>
#include <tinos/smp.h>
#include <tinos/string.h>
#include <tinos/tar.h>
#include <tinos/thread.h>
#include <tinos/timer.h>
#include <tinos/tty.h>

typedef struct {
    int width;
    int height;
} screen_mode_t;

static const screen_mode_t screen_modes[] = {
    { 1024, 768 },
    { 800, 600 },
    { 640, 480 },
};

/* How long the text-mode stage stays on screen before the desktop starts. */
#define BOOT_STEP_DELAY_MS  180
#define BOOT_HOLD_MS        1200

static const char *boot_cmdline = "";

typedef struct {
    block_device_t *device;
    size_t mount_index;
} storage_mount_job_t;

static void storage_mount_worker(void *arg)
{
    storage_mount_job_t *job = arg;
    storage_mount_device(job->device, &job->mount_index);
    thread_exit();
}

static void boot_banner(void)
{
    vga_text_set_color(VGA_LCYAN, VGA_BLACK);
    vga_text_write("\n  " TINOS_NAME " " TINOS_VERSION
                   " - a small graphical operating system\n");
    vga_text_set_color(VGA_DGRAY, VGA_BLACK);
    vga_text_write("  ============================================="
                   "==========\n\n");
    vga_text_set_color(VGA_LGRAY, VGA_BLACK);
}

/* Print "[ OK ] message" style progress with a short, visible pause. */
static void boot_step(const char *fmt, ...) PRINTFLIKE(1, 2);

static void boot_step(const char *fmt, ...)
{
    char    line[160];
    va_list ap;

    va_start(ap, fmt);
    kvsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    vga_text_set_color(VGA_LGREEN, VGA_BLACK);
    vga_text_write("  [ OK ] ");
    vga_text_set_color(VGA_LGRAY, VGA_BLACK);
    vga_text_write(line);
    vga_text_write("\n");
    serial_write("[boot] ");
    serial_write(line);
    serial_write("\n");
}

static void boot_note(const char *fmt, ...) PRINTFLIKE(1, 2);

static void boot_note(const char *fmt, ...)
{
    char    line[160];
    va_list ap;

    va_start(ap, fmt);
    kvsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    vga_text_set_color(VGA_DGRAY, VGA_BLACK);
    vga_text_write("         ");
    vga_text_write(line);
    vga_text_write("\n");
    vga_text_set_color(VGA_LGRAY, VGA_BLACK);
    serial_write("[boot]   ");
    serial_write(line);
    serial_write("\n");
}

static void boot_fail(const char *fmt, ...) PRINTFLIKE(1, 2);

static void boot_fail(const char *fmt, ...)
{
    char    line[160];
    va_list ap;

    va_start(ap, fmt);
    kvsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    vga_text_set_color(VGA_LRED, VGA_BLACK);
    vga_text_write("  [FAIL] ");
    vga_text_set_color(VGA_LGRAY, VGA_BLACK);
    vga_text_write(line);
    vga_text_write("\n");
    serial_write("[boot] FAIL ");
    serial_write(line);
    serial_write("\n");
}

static void pause_ms(uint32_t ms)
{
    timer_sleep(ms);
}

/* Reserve bootloader payloads before the heap acquires its first frame. */
static void reserve_initrd(const boot_info_t *info)
{
    if (info->initrd_end > info->initrd_start) {
        klog("[boot] ramfs module 0x%08x..0x%08x\n",
             info->initrd_start, info->initrd_end);
        pmm_reserve_range(info->initrd_start,
                          info->initrd_end - info->initrd_start);
    }
}

static size_t import_userland(const boot_info_t *info)
{
    if (info->initrd_end <= info->initrd_start)
        return 0;
    return tar_extract((const void *)(uintptr_t)info->initrd_start,
                       info->initrd_end - info->initrd_start, "/apps");
}

void kmain(uint32_t magic, uint32_t info_addr)
{
    const boot_info_t *info = (const boot_info_t *)(uintptr_t)info_addr;

    serial_init();
    vga_text_init();
    boot_banner();

    klog("[boot] " TINOS_NAME " " TINOS_VERSION " starting, magic=0x%08x "
         "boot_info=0x%08x\n",
         magic, info_addr);

    if (magic != TINOS_BOOT_MAGIC || !info ||
        info->size != sizeof(*info) ||
        info->version != TINOS_BOOT_VERSION ||
        info->memory_count > BOOT_MEMORY_ENTRIES) {
        boot_fail("boot: invalid handoff 0x%08x", magic);
        panic("invalid boot information");
    }

    boot_cmdline = info->command_line;
    boot_step("boot: TinOS BIOS loader");

    /* Platform */
    gdt_init();
    boot_step("gdt: flat 32-bit segments loaded");
    idt_init();
    boot_step("idt: vectors installed, i8259 PIC remapped");

    if (!arch_fpu_init())
        panic("CPU lacks FXSAVE/SSE2 - TinOS requires them");
    boot_step("fpu: SSE2 (FXSAVE/FXRSTOR) enabled");

    paging_init();
    boot_step("mm: paging enabled, identity-mapped");

    timer_init(TIMER_HZ);
    rtc_init();
    sti();
    boot_step("timer: PIT %u Hz, rtc online", TIMER_HZ);
    pause_ms(BOOT_STEP_DELAY_MS);

    char cpu[64];
    cpu_brand_string(cpu, sizeof(cpu));
    boot_note("cpu: %s", cpu);

    /* Memory */
    pmm_init(info);
    reserve_initrd(info);
    heap_init();
    async_init();
    bus_init();
    thread_init();
    ktimer_init();
    driver_model_init();
    e1000_driver_register();
    virtio_blk_driver_register();
    audio_driver_register();
    audio_codecs_init();
    image_codecs_init();
    boot_step("mm: heap and physical allocator online");
    boot_note("async: worker pool online");
    boot_note("sched: kernel threads online (private stacks, PIT preemption)");
    boot_note("mm: %u MiB usable, %u regions, %u MiB free",
              (uint32_t)(pmm_total_bytes() / (1024 * 1024)),
              (uint32_t)pmm_region_count(),
              (uint32_t)(pmm_free_bytes() / (1024 * 1024)));
    pause_ms(BOOT_STEP_DELAY_MS);

    /* SMP */
    size_t cpus = smp_init(info);
    if (cpus > 1)
        boot_step("smp: %u cpus online", (uint32_t)cpus);
    else
        boot_note("smp: single-core");

    /* Input */
    keyboard_init();
    boot_step("i8042: keyboard on irq1");
    bool mouse = mouse_init();
    if (mouse)
        boot_step("i8042: mouse on irq12");
    else
        boot_note("i8042: no mouse detected, keyboard only");
    pause_ms(BOOT_STEP_DELAY_MS);

    /* Storage */
    size_t ata_disks = ata_pio_init();
    boot_note("ata: %u disk(s) detected (PIO)", (uint32_t)ata_disks);

    /* Filesystem */
    fs_init();
    boot_step("vfs: tmpfs mounted on /");
    size_t mounted = 0;
    for (size_t i = 0; i < ata_pio_count(); i++)
        storage_mount_device(ata_pio_device(i), &mounted);
    boot_note("vfs: %u nodes in tree", (uint32_t)fs_node_count());
    pause_ms(BOOT_STEP_DELAY_MS);

    /* Executables remain data until launched through the ring-3 loader. */
    size_t executables = import_userland(info);
    if (executables)
        boot_step("userland: imported %u ELF32 file(s)", (uint32_t)executables);
    else
        boot_note("userland: no executable archive loaded");
    pause_ms(BOOT_STEP_DELAY_MS);

    /* Optional self test */
    if (strstr(boot_cmdline, "selftest")) {
        boot_step("selftest: running (log on ttyS0)");
        int failures = selftest_run();
        if (failures)
            boot_fail("selftest: %d failure(s), see serial log", failures);
        else
            boot_step("selftest: all tests passed");
        pause_ms(BOOT_STEP_DELAY_MS);
    }

    /* Devices */
    size_t devices = pci_scan();
    size_t bound = driver_bind_all();
    for (size_t i = 0; i < virtio_blk_count(); i++)
        storage_mount_device(virtio_blk_device(i), &mounted);
    size_t usb_disks = uhci_usb_init();
    static storage_mount_job_t usb_mount;
    if (usb_disks) {
        usb_mount.device = uhci_usb_device(0);
        usb_mount.mount_index = mounted;
        kernel_thread_t *worker = thread_spawn_pinned("usb-mount", storage_mount_worker,
                                               &usb_mount);
        if (worker)
            thread_detach(worker);
        else
            storage_mount_device(usb_mount.device, &usb_mount.mount_index);
    }
    bool network = net_init();
    if (network)
        net_async_init();
    boot_step("pci: %u device(s) found", (uint32_t)devices);
    boot_note("driver: %u devices, %u drivers bound",
              (uint32_t)device_count(), (uint32_t)bound);
    boot_note("net: %s", network ? "dhcp configured" : "unavailable");

    if (strstr(boot_cmdline, "tinos.mode=text")) {
        tty_start();
        kernel_loop_run(tty_step, NULL);
    }

    boot_note("mm: %u KiB free before graphics",
              (uint32_t)(pmm_free_bytes() / 1024));

    /* Graphics is optional: preserve the text console if no adapter or
     * sufficient contiguous RAM is available for a back buffer. */
    for (;;) {
        vga_text_set_color(VGA_YELLOW, VGA_BLACK);
        vga_text_write("\n  Starting the graphical desktop ...\n");
        vga_text_set_color(VGA_LGRAY, VGA_BLACK);
        pause_ms(BOOT_HOLD_MS);

        bool graphics = false;
        for (size_t i = 0; i < ARRAY_SIZE(screen_modes); i++) {
            size_t bytes = (size_t)screen_modes[i].width *
                           (size_t)screen_modes[i].height * 4;
            if (pmm_free_bytes() < bytes + 1024 * 1024)
                continue;
            if (gfx_init(screen_modes[i].width, screen_modes[i].height)) {
                graphics = true;
                break;
            }
        }
        if (graphics)
            break;

        boot_fail("no supported framebuffer mode or insufficient RAM");
        tty_start();
        kernel_loop_run(tty_step, NULL);
    }
    vga_text_disable(); /* text console memory is no longer displayed */
    klog("[boot] graphics mode active, entering desktop\n");

    /* Desktop */
    desktop_start();
    kernel_loop_run(desktop_step, NULL);

    panic("desktop loop returned unexpectedly");
}
