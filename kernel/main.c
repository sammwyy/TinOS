/* TinOS kernel entry point and boot sequence.
 *
 * kmain() runs the text-mode stage: it brings up the descriptor tables,
 * interrupts, memory, input, the filesystem and finally the graphics mode,
 * reporting each step on screen and on the serial port.  Once the display
 * has been switched to a linear framebuffer the desktop takes over and
 * never returns.
 */

#include <tinos/abi.h>
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
#include <tinos/elf.h>
#include <tinos/fs.h>
#include <tinos/gfx.h>
#include <tinos/heap.h>
#include <tinos/image.h>
#include <tinos/input.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/mm.h>
#include <tinos/native_app.h>
#include <tinos/net.h>
#include <tinos/multiboot2.h>
#include <tinos/pci.h>
#include <tinos/printf.h>
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

static const mb_tag_module_t *find_ramfs_module(const mb_info_t *mbi)
{
    MB_FOR_EACH_TAG(mbi, tag)
    {
        if (tag->type == MB_TAG_MODULE)
            return (const mb_tag_module_t *)tag;
    }
    return NULL;
}

/* GRUB loads module2 payloads into RAM before the kernel runs, at addresses
 * the pmm bitmap does not know about yet. Called right after pmm_init(),
 * before heap_init() takes its first frame, so nothing can land on top of
 * the ramfs or the arena native apps get loaded into. */
static void reserve_native_app_memory(const mb_info_t *mbi)
{
    const mb_tag_module_t *mod = find_ramfs_module(mbi);
    if (mod) {
        klog("[boot] ramfs module 0x%08x..0x%08x\n",
             mod->mod_start, mod->mod_end);
        pmm_reserve_range(mod->mod_start, mod->mod_end - mod->mod_start);
    }
    pmm_reserve_range(NATIVE_APP_BASE, NATIVE_APP_ARENA_SIZE);
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
        s[--n] = '\0';
    return s;
}

/* app.conf is a flat key=value list (name=, summary=, category=, icon=);
 * blank lines and lines starting with # are ignored. icon, if present,
 * names a file in the same directory for load_icon() below - out of
 * app_entry_t's fields, only that one takes another pass to resolve.
 *
 * optimize("O0") is load-bearing: at -O2 with -mgeneral-regs-only (the
 * kernel's normal flags), GCC miscompiles this function - the values it
 * produces come out correct, but something about the code it generates
 * corrupts state that only shows up much later, as a hang the first time
 * the desktop calls into a loaded app's launch() after switching start
 * menu categories. Confirmed by bisection: reverting to -O0 for just this
 * function is enough to make it go away. */
__attribute__((optimize("O0")))
static void parse_manifest(const fs_node_t *node, app_entry_t *entry,
                           char *icon_file, size_t icon_file_size)
{
    char   buf[1024];
    size_t size = fs_size(node);
    if (size >= sizeof(buf))
        size = sizeof(buf) - 1;
    memcpy(buf, fs_data(node), size);
    buf[size] = '\0';

    char *line = buf;
    while (line) {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';
        char *l = trim(line);
        char *eq = l[0] && l[0] != '#' ? strchr(l, '=') : NULL;
        if (eq) {
            *eq = '\0';
            char *key   = trim(l);
            char *value = trim(eq + 1);
            if (!strcmp(key, "name"))
                entry->name = kstrdup(value);
            else if (!strcmp(key, "summary"))
                entry->summary = kstrdup(value);
            else if (!strcmp(key, "category"))
                entry->category = kstrdup(value);
            else if (!strcmp(key, "icon"))
                strlcpy(icon_file, value, icon_file_size);
        }
        line = nl ? nl + 1 : NULL;
    }
}

static image_t load_icon(const fs_node_t *app_dir, const char *icon_file)
{
    image_t image = { 0 };
    if (!icon_file[0])
        return image;
    fs_node_t *icon_node = fs_child(app_dir, icon_file);
    if (!icon_node)
        return image;
    const char *dot = strrchr(icon_file, '.');
    image_decode(fs_data(icon_node), fs_size(icon_node), dot ? dot + 1 : "",
                &image);
    return image;
}

/* app_main()'s real signature: every field a loaded app needs to register
 * itself with, passed by value instead of through a pointer to memory this
 * function owns - a pointer handed across the ABI and written back
 * through, even copied out before the app returns, corrupted apps'
 * launch() calls later at unrelated, unpredictable points (confirmed by
 * bisecting against the previous commit). Passed as plain arguments, each
 * app still builds and owns its own local app_entry_t exactly as it did
 * before app.conf existed. */
typedef void (*app_main_fn)(const tinos_abi_t *abi, const char *name,
                            const char *summary, const char *category,
                            icon_t icon, image_t custom_icon);

/* Unpacks the ramfs module into /apps and runs the *.elf inside every
 * subdirectory there once, each at its own fixed slot in the
 * NATIVE_APP_BASE arena. A subdirectory's app.conf and icon file (see
 * parse_manifest() and load_icon() above) become the arguments passed to
 * its app_main(), which builds its own app_entry_t and registers it. */
static size_t load_native_apps(const mb_info_t *mbi)
{
    const mb_tag_module_t *mod = find_ramfs_module(mbi);
    if (!mod)
        return 0;

    const void *image = (const void *)(uintptr_t)mod->mod_start;
    size_t      size   = mod->mod_end - mod->mod_start;
    if (tar_extract(image, size, "/apps") == 0)
        return 0;

    tinos_abi_t abi = tinos_abi_build();
    size_t      loaded = 0;
    uint32_t    used_slots = 0;
    fs_node_t  *apps_dir;
    if (fs_resolve(fs_root(), "/apps", &apps_dir) != FS_OK)
        return 0;

    for (fs_node_t *dir = apps_dir->children; dir; dir = dir->next) {
        if (dir->type != FS_DIR)
            continue;

        fs_node_t *elf_node = NULL;
        for (fs_node_t *child = dir->children; child; child = child->next) {
            size_t len = strlen(child->name);
            if (child->type == FS_FILE && len > 4 &&
                strcmp(child->name + len - 4, ".elf") == 0) {
                elf_node = child;
                break;
            }
        }
        if (!elf_node) {
            klog("[boot] apps: %s has no .elf, skipped\n", dir->name);
            continue;
        }

        app_entry_t entry = {
            .name = dir->name, .summary = "", .category = "Other",
            .icon = ICON_FILE,
        };
        char        icon_file[64] = "";
        fs_node_t  *manifest = fs_child(dir, "app.conf");
        if (manifest)
            parse_manifest(manifest, &entry, icon_file, sizeof(icon_file));
        entry.custom_icon = load_icon(dir, icon_file);

        void *elf_entry = NULL;
        uintptr_t entry_address;
        if (!elf_entry_address(fs_data(elf_node), fs_size(elf_node),
                               &entry_address) ||
            entry_address < NATIVE_APP_BASE ||
            entry_address - NATIVE_APP_BASE >= NATIVE_APP_ARENA_SIZE) {
            klog("[boot] apps: invalid address in %s\n", elf_node->name);
            continue;
        }
        size_t slot = (entry_address - NATIVE_APP_BASE) / NATIVE_APP_SLOT_SIZE;
        uintptr_t slot_base = NATIVE_APP_BASE + slot * NATIVE_APP_SLOT_SIZE;
        if (!pmm_range_is_ram(slot_base, NATIVE_APP_SLOT_SIZE)) {
            klog("[boot] apps: %s slot is outside usable RAM\n", elf_node->name);
            continue;
        }
        if (slot_base < mod->mod_end &&
            mod->mod_start < slot_base + NATIVE_APP_SLOT_SIZE) {
            klog("[boot] apps: %s slot overlaps the ramfs module\n",
                 elf_node->name);
            continue;
        }
        if (used_slots & (1u << slot)) {
            klog("[boot] apps: duplicate slot in %s\n", elf_node->name);
            continue;
        }
        if (!elf_load(fs_data(elf_node), fs_size(elf_node), slot_base,
                      NATIVE_APP_SLOT_SIZE, &elf_entry)) {
            klog("[boot] apps: failed to load %s\n", elf_node->name);
            continue;
        }
        used_slots |= 1u << slot;
        ((app_main_fn)elf_entry)(&abi, entry.name, entry.summary,
                                 entry.category, entry.icon,
                                 entry.custom_icon);
        loaded++;
    }
    return loaded;
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
        boot_fail("multiboot2: bad magic 0x%08x (expected 0x%08x)",
                  magic, MULTIBOOT2_BOOTLOADER_MAGIC);
        panic("bad multiboot2 magic 0x%08x - expected 0x%08x", magic,
              MULTIBOOT2_BOOTLOADER_MAGIC);
    }

    const char *loader = NULL;
    read_multiboot_strings(mbi, &loader);
    boot_step("multiboot2: bootloader=%s", loader);

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
    pmm_init(mbi);
    reserve_native_app_memory(mbi);
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
    size_t cpus = smp_init(mbi);
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

    /* Native applications */
    size_t native_apps = load_native_apps(mbi);
    if (native_apps)
        boot_step("apps: loaded %u native app(s) from the ramfs",
                  (uint32_t)native_apps);
    else
        boot_note("apps: no native apps loaded");
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

    if (strstr(boot_cmdline, "tinos.mode=text"))
        tty_run();

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
        tty_run();
    }
    vga_text_disable(); /* text console memory is no longer displayed */
    klog("[boot] graphics mode active, entering desktop\n");

    /* Desktop */
    desktop_run();

    panic("desktop_run() returned unexpectedly");
}
