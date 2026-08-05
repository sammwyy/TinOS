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
#include <tinos/console.h>
#include <tinos/desktop.h>
#include <tinos/driver.h>
#include <tinos/fs.h>
#include <tinos/gfx.h>
#include <tinos/heap.h>
#include <tinos/input.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/mm.h>
#include <tinos/multiboot2.h>
#include <tinos/pci.h>
#include <tinos/printf.h>
#include <tinos/selftest.h>
#include <tinos/string.h>
#include <tinos/thread.h>
#include <tinos/timer.h>

#define SCREEN_WIDTH  1024
#define SCREEN_HEIGHT 768

/* How long the text-mode stage stays on screen before the desktop starts. */
#define BOOT_STEP_DELAY_MS  180
#define BOOT_HOLD_MS        1200

static const char *boot_cmdline = "";

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

/* Locate the command line and bootloader name tags for reporting. */
static void read_multiboot_strings(const mb_info_t *mbi, const char **loader)
{
    *loader = "unknown";
    MB_FOR_EACH_TAG(mbi, tag)
    {
        const mb_tag_string_t *s = (const mb_tag_string_t *)tag;
        if (tag->type == MB_TAG_CMDLINE)
            boot_cmdline = s->string;
        else if (tag->type == MB_TAG_BOOT_LOADER)
            *loader = s->string;
    }
}

void kmain(uint32_t magic, uint32_t mbi_addr)
{
    const mb_info_t *mbi = (const mb_info_t *)(uintptr_t)mbi_addr;

    serial_init();
    vga_text_init();
    boot_banner();

    klog("[boot] " TINOS_NAME " " TINOS_VERSION " starting, magic=0x%08x "
         "mbi=0x%08x\n",
         magic, mbi_addr);

    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        boot_fail("not loaded by a Multiboot2 bootloader (magic 0x%08x)",
                  magic);
        panic("bad multiboot2 magic 0x%08x - expected 0x%08x", magic,
              MULTIBOOT2_BOOTLOADER_MAGIC);
    }

    const char *loader = NULL;
    read_multiboot_strings(mbi, &loader);
    boot_step("Multiboot2 handover from %s", loader);

    /* --- platform ------------------------------------------------- */
    gdt_init();
    boot_step("Global descriptor table installed (flat 32-bit model)");
    idt_init();
    boot_step("Interrupt descriptor table and 8259A PIC ready");

    timer_init(TIMER_HZ);
    rtc_init();
    sti();
    boot_step("System timer running at %u Hz, RTC online", TIMER_HZ);
    pause_ms(BOOT_STEP_DELAY_MS);

    char cpu[64];
    cpu_brand_string(cpu, sizeof(cpu));
    boot_note("CPU: %s", cpu);

    /* --- memory --------------------------------------------------- */
    pmm_init(mbi);
    heap_init();
    async_init();
    thread_init();
    ktimer_init();
    driver_model_init();
    boot_step("Memory manager online");
    boot_note("Asynchronous worker runtime online (cooperative mode)");
    boot_note("Kernel thread scheduler online (private stacks, cooperative)");
    boot_note("%u MiB usable in %u regions, %u MiB free",
              (uint32_t)(pmm_total_bytes() / (1024 * 1024)),
              (uint32_t)pmm_region_count(),
              (uint32_t)(pmm_free_bytes() / (1024 * 1024)));
    pause_ms(BOOT_STEP_DELAY_MS);

    /* --- input ---------------------------------------------------- */
    keyboard_init();
    boot_step("PS/2 keyboard driver attached to IRQ1");
    bool mouse = mouse_init();
    if (mouse)
        boot_step("PS/2 mouse driver attached to IRQ12");
    else
        boot_note("No PS/2 mouse detected - keyboard control only");
    pause_ms(BOOT_STEP_DELAY_MS);

    /* --- filesystem ----------------------------------------------- */
    fs_init();
    boot_step("In-memory filesystem mounted at /");
    boot_note("%u nodes in the initial tree", (uint32_t)fs_node_count());
    pause_ms(BOOT_STEP_DELAY_MS);

    /* --- optional self test --------------------------------------- */
    if (strstr(boot_cmdline, "selftest")) {
        boot_step("Running kernel self-tests (results on COM1)");
        int failures = selftest_run();
        if (failures)
            boot_fail("%d self-test failure(s) - see the serial log", failures);
        else
            boot_step("All kernel self-tests passed");
        pause_ms(BOOT_STEP_DELAY_MS);
    }

    /* --- graphics ------------------------------------------------- */
    size_t devices = pci_scan();
    size_t bound = driver_bind_all();
    boot_step("PCI bus scanned, %u devices", (uint32_t)devices);
    boot_note("Driver model online: %u devices, %u drivers bound",
              (uint32_t)device_count(), (uint32_t)bound);

    vga_text_set_color(VGA_YELLOW, VGA_BLACK);
    vga_text_printf("\n  Starting the graphical desktop at %dx%d ...\n",
                    SCREEN_WIDTH, SCREEN_HEIGHT);
    vga_text_set_color(VGA_LGRAY, VGA_BLACK);
    pause_ms(BOOT_HOLD_MS);

    if (!gfx_init(SCREEN_WIDTH, SCREEN_HEIGHT)) {
        boot_fail("VGA graphics initialisation failed");
        panic("no usable VGA framebuffer.\n"
              "TinOS needs a Bochs/QEMU DISPI capable adapter.\n"
              "Try: qemu-system-i386 -vga std -cdrom tinos.iso");
    }
    vga_text_disable(); /* text console memory is no longer displayed */
    klog("[boot] graphics mode active, entering desktop\n");

    /* --- desktop -------------------------------------------------- */
    desktop_run();

    panic("desktop_run() returned unexpectedly");
}
