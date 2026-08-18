#include <tinos/arch.h>
#include <tinos/fs.h>
#include <tinos/gfx.h>
#include <tinos/heap.h>
#include <tinos/kernel.h>
#include <tinos/printf.h>
#include <tinos/sh.h>
#include <tinos/ui.h>

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
