/* The two informational windows: About and System Information. */

#include <tinos/arch.h>
#include <tinos/fs.h>
#include <tinos/gfx.h>
#include <tinos/heap.h>
#include <tinos/kernel.h>
#include <tinos/net.h>
#include <tinos/printf.h>
#include <tinos/sh.h>
#include <tinos/string.h>
#include <tinos/ui.h>

/* About */

static const char *const about_lines[] = {
    "TinOS " TINOS_VERSION,
    "A tiny graphical operating system for your computer.",
    "",
    "It includes a desktop with a start menu, a command",
    "line, a file manager, a text editor and more.",
    "",
    "Everything here runs from memory, so the system feels",
    "fast and responsive, just like a real desktop.",
    "",
    "Shortcuts: Win/Ctrl+Esc start menu, Alt+Tab switch windows,",
    "Alt+F4 close the current window.",
};

static void about_on_draw(window_t *win, rect_t client)
{
    gfx_fill_rect(client.x, client.y, client.w, client.h, COLOR_WINDOW_BG);
    gfx_gradient_v(client.x, client.y, client.w, 74, COLOR_ACCENT_DARK,
                   COLOR_WINDOW_BG);
    widget_icon(client.x + 18, client.y + 14, 44, ICON_TINOS);
    gfx_text(client.x + 76, client.y + 22, TINOS_NAME " " TINOS_VERSION,
             COLOR_TEXT);
    gfx_text(client.x + 76, client.y + 42, "Lightweight and built to run on anything",
             COLOR_TEXT_DIM);

    int y = client.y + 88;
    for (size_t i = 0; i < ARRAY_SIZE(about_lines); i++, y += FONT_H + 2)
        gfx_text(client.x + 18, y, about_lines[i],
                 about_lines[i][0] == ' ' ? COLOR_TEXT_DIM : COLOR_TEXT);
}

static const app_class_t about_class = {
    .name    = "about",
    .on_draw = about_on_draw,
};

window_t *app_about_open(const char *arg)
{
    return wm_open(&about_class, 512, 420, "About TinOS", arg);
}

/* System information */

typedef struct {
    uint32_t last_second;
} sysinfo_state_t;

static void format_size(size_t bytes, char *buf, size_t size)
{
    if (bytes < 1024)
        ksnprintf(buf, size, "%u B", (uint32_t)bytes);
    else if (bytes < 1024 * 1024)
        ksnprintf(buf, size, "%u KiB", (uint32_t)(bytes / 1024));
    else
        ksnprintf(buf, size, "%u.%u MiB", (uint32_t)(bytes / (1024 * 1024)),
                  (uint32_t)((bytes % (1024 * 1024)) * 10 / (1024 * 1024)));
}

static void row(int x, int *y, const char *label, const char *value)
{
    gfx_text(x, *y, label, COLOR_TEXT_DIM);
    gfx_text(x + 130, *y, value, COLOR_TEXT);
    *y += FONT_H + 6;
}

static void sysinfo_on_draw(window_t *win, rect_t client)
{
    sys_info_t info;
    sys_info_get(&info);

    heap_stats_t heap;
    heap_get_stats(&heap);

    gfx_fill_rect(client.x, client.y, client.w, client.h, COLOR_WINDOW_BG);
    widget_icon(client.x + 16, client.y + 14, 32, ICON_CHIP);
    gfx_text(client.x + 60, client.y + 22, "System Information", COLOR_TEXT);
    gfx_hline(client.x + 12, client.y + 56, client.w - 24, COLOR_BORDER);

    int x = client.x + 18;
    int y = client.y + 70;
    char buf[80], buf2[32];

    row(x, &y, "system", TINOS_NAME " " TINOS_VERSION);
    row(x, &y, "platform", info.platform);
    row(x, &y, "cpu", info.cpu);

    ksnprintf(buf, sizeof(buf), "%dx%d, 32 bpp", info.screen_width,
              info.screen_height);
    row(x, &y, "display", buf);

    uint32_t seconds = info.uptime_ms / 1000;
    ksnprintf(buf, sizeof(buf), "%02u:%02u:%02u", seconds / 3600,
              (seconds / 60) % 60, seconds % 60);
    row(x, &y, "uptime", buf);
    row(x, &y, "clock", info.datetime);

    y += 6;
    gfx_hline(client.x + 12, y, client.w - 24, COLOR_BORDER);
    y += 10;

    format_size((size_t)info.mem_total, buf2, sizeof(buf2));
    format_size((size_t)info.mem_free, buf, sizeof(buf));
    char both[80];
    ksnprintf(both, sizeof(both), "%s total, %s free", buf2, buf);
    row(x, &y, "memory", both);

    format_size(heap.heap_total, buf2, sizeof(buf2));
    format_size(heap.heap_used, buf, sizeof(buf));
    ksnprintf(both, sizeof(both), "%s reserved, %s used", buf2, buf);
    row(x, &y, "kernel heap", both);

    ksnprintf(both, sizeof(both), "%u allocations, %u frees",
              (uint32_t)heap.alloc_count, (uint32_t)heap.free_count);
    row(x, &y, "heap activity", both);

    format_size(fs_total_bytes(), buf, sizeof(buf));
    ksnprintf(both, sizeof(both), "%u nodes, %s of file data",
              (uint32_t)fs_node_count(), buf);
    row(x, &y, "filesystem", both);

    ksnprintf(both, sizeof(both), "%u open", (uint32_t)wm_count());
    row(x, &y, "windows", both);

    /* Memory usage bar. */
    y += 8;
    int bar_w = client.w - 36;
    int used  = info.mem_total ? (int)((info.mem_total - info.mem_free) *
                                       (uint64_t)bar_w / info.mem_total)
                               : 0;
    gfx_fill_rect(x, y, bar_w, 14, RGB(0x18, 0x1d, 0x28));
    gfx_fill_rect(x, y, CLAMP(used, 1, bar_w), 14, COLOR_ACCENT);
    gfx_rect(x, y, bar_w, 14, COLOR_BORDER);
}

static bool sysinfo_on_tick(window_t *win)
{
    sysinfo_state_t *state = win->priv;
    if (!state)
        return false;
    uint32_t second = (uint32_t)(timer_ticks() / 1000);
    if (second == state->last_second)
        return false;
    state->last_second = second;
    return true;
}

static void sysinfo_on_open(window_t *win, const char *arg)
{
    win->priv = kcalloc(1, sizeof(sysinfo_state_t));
}

static void sysinfo_on_close(window_t *win)
{
    kfree(win->priv);
    win->priv = NULL;
}

static const app_class_t sysinfo_class = {
    .name     = "sysinfo",
    .on_open  = sysinfo_on_open,
    .on_close = sysinfo_on_close,
    .on_draw  = sysinfo_on_draw,
    .on_tick  = sysinfo_on_tick,
};

window_t *app_sysinfo_open(const char *arg)
{
    return wm_open(&sysinfo_class, 480, 420, "System Information", arg);
}

/* Application registry (drives the start menu) */

static window_t *launch_terminal(const char *arg)
{
    return app_terminal_open(arg);
}

static window_t *launch_explorer(const char *arg)
{
    return app_explorer_open(arg ? arg : TINOS_DEFAULT_HOME);
}

static window_t *launch_editor(const char *arg)
{
    return app_editor_open(arg ? arg : "/README.txt");
}

static window_t *launch_image_viewer(const char *arg)
{
    return app_image_viewer_open(arg);
}

static window_t *launch_media_player(const char *arg)
{
    return app_media_player_open(arg);
}

static window_t *launch_control_panel(const char *arg)
{
    return app_control_panel_open(arg);
}

/* Calculator is not in this table: it ships as a native ELF (see
 * apps/native/calculator/) and registers itself with apps_register() once
 * the kernel loads it from the ramfs. Everything else here is still a
 * builtin compiled straight into the kernel image. */
#define REGISTRY_CAPACITY 16

static app_entry_t registry[REGISTRY_CAPACITY] = {
    { "File Explorer", "browse files", "Accessories", ICON_FOLDER,
      launch_explorer },
    { "Text Editor", "write text", "Accessories", ICON_EDITOR,
      launch_editor },
    { "Image Viewer", "view JPG, PNG and WebP images", "Multimedia", ICON_FILE,
      launch_image_viewer },
    { "Media Player", "play WAV and Ogg Vorbis audio", "Multimedia", ICON_FILE,
      launch_media_player },
    { "Terminal", "command line", "System", ICON_TERMINAL,
      launch_terminal },
    { "System Information", "system details", "System", ICON_CHIP,
      app_sysinfo_open },
    { "Control Panel", "network and system settings", "System", ICON_CHIP,
      launch_control_panel },
    { "About TinOS", "version and info", "System", ICON_INFO,
      app_about_open },
};
static size_t registry_count = 8;

const app_entry_t *apps_registry(size_t *count)
{
    *count = registry_count;
    return registry;
}

bool apps_register(const app_entry_t *entry)
{
    if (registry_count >= REGISTRY_CAPACITY)
        return false;
    registry[registry_count++] = *entry;
    return true;
}
