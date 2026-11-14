/* Task Manager: live CPU and memory graphs, the process table and
 * "End task". Everything is sampled once a second through PROCLIST and
 * LOADINFO; nothing is kept beyond a minute of history. */
#include <tinos/string.h>
#include <tinos/widgets.h>

#define HISTORY 60
#define MAX_ROWS 16
#define ROW_H 20
#define GRAPH_H 64
#define HEADER_H 44
#define BUTTON_W 96

typedef struct {
    tinos_process_info_t info;
    char name[32];
    char icon[20];
    unsigned cpu_percent;
} row_t;

static tinos_widget_view_t ui;
static row_t rows[MAX_ROWS];
static unsigned row_count;
static int selected = -1;
static uint32_t selected_pid;
static uint8_t cpu_history[HISTORY];
static uint8_t mem_history[HISTORY];
static unsigned history_count;
static tinos_load_info_t load, last_load;
static char status_line[64];

/* Previous sample per PID: turns CPU totals into a percentage and avoids
 * rereading the app manifest every second. */
static struct {
    uint32_t pid, cpu_ms;
    char name[32];
    char icon[20];
} previous[MAX_ROWS];

static void append(char *out, size_t size, const char *text)
{
    size_t used = strlen(out);
    if (used < size)
        tinos_strlcpy(out + used, text, size - used);
}

static void append_u32(char *out, size_t size, uint32_t value)
{
    char number[11];
    tinos_u32(number, value);
    append(out, size, number);
}

static void format_kib(char *out, size_t size, uint32_t kib)
{
    out[0] = '\0';
    if (kib >= 10240) {
        append_u32(out, size, kib / 1024);
        append(out, size, " MiB");
    } else {
        append_u32(out, size, kib);
        append(out, size, " KiB");
    }
}

/* "/apps/gui/image_viewer.elf" -> the manifest's name= and icon=, else
 * "image_viewer" with the generic application icon. */
static void describe(const char *path, char *out, size_t size, char *icon,
                     size_t icon_size)
{
    tinos_strlcpy(icon, "app", icon_size);
    const char *base = path;
    for (const char *c = path; *c; c++)
        if (*c == '/')
            base = c + 1;
    tinos_strlcpy(out, base[0] ? base : "?", size);
    size_t length = strlen(out);
    if (length > 4 && strcmp(out + length - 4, ".elf") == 0)
        out[length - 4] = '\0';

    char manifest[80];
    tinos_strlcpy(manifest, path, sizeof(manifest));
    length = strlen(manifest);
    if (length <= 4 || strcmp(manifest + length - 4, ".elf") != 0)
        return;
    tinos_strlcpy(manifest + length - 4, ".app", sizeof(manifest) - length + 4);
    int fd = tinos_open(manifest);
    if (fd < 0)
        return;
    char text[256];
    int got = tinos_read(fd, text, sizeof(text) - 1);
    tinos_close(fd);
    if (got <= 0)
        return;
    text[got] = '\0';
    for (char *line = text; *line;) {
        char *end = line;
        while (*end && *end != '\n')
            end++;
        char saved = *end;
        *end = '\0';
        if (strncmp(line, "name=", 5) == 0 && line[5])
            tinos_strlcpy(out, line + 5, size);
        else if (strncmp(line, "icon=", 5) == 0 && line[5])
            tinos_strlcpy(icon, line + 5, icon_size);
        if (!saved)
            break;
        line = end + 1;
    }
}

static void sample(void)
{
    last_load = load;
    if (tinos_loadinfo(&load) < 0)
        return;
    uint32_t elapsed = load.uptime_ms - last_load.uptime_ms;

    unsigned cpu = 0;
    if (last_load.uptime_ms && elapsed) {
        uint32_t idle = load.idle_ms - last_load.idle_ms;
        cpu = idle >= elapsed ? 0 : (elapsed - idle) * 100 / elapsed;
    }
    unsigned mem = load.memory_total_kib ?
        (load.memory_total_kib - load.memory_free_kib) * 100 /
        load.memory_total_kib : 0;
    if (history_count == HISTORY) {
        memmove(cpu_history, cpu_history + 1, HISTORY - 1);
        memmove(mem_history, mem_history + 1, HISTORY - 1);
        history_count--;
    }
    cpu_history[history_count] = (uint8_t)cpu;
    mem_history[history_count] = (uint8_t)mem;
    history_count++;

    row_count = 0;
    tinos_process_info_t info;
    for (unsigned i = 0; row_count < MAX_ROWS &&
                         tinos_proclist(i, &info) == 1; i++) {
        if (info.state == TINOS_PROC_EXITED)
            continue;
        row_t *row = &rows[row_count++];
        row->info = info;
        row->cpu_percent = 0;
        row->name[0] = '\0';
        for (unsigned p = 0; p < MAX_ROWS; p++)
            if (previous[p].pid == info.pid) {
                uint32_t used = info.cpu_ms - previous[p].cpu_ms;
                if (elapsed)
                    row->cpu_percent = used >= elapsed ? 100 :
                                       used * 100 / elapsed;
                tinos_strlcpy(row->name, previous[p].name, sizeof(row->name));
                tinos_strlcpy(row->icon, previous[p].icon, sizeof(row->icon));
            }
        if (!row->name[0])
            describe(info.path, row->name, sizeof(row->name), row->icon,
                     sizeof(row->icon));
    }
    for (unsigned p = 0; p < MAX_ROWS; p++) {
        previous[p].pid = p < row_count ? rows[p].info.pid : 0;
        previous[p].cpu_ms = p < row_count ? rows[p].info.cpu_ms : 0;
        if (p < row_count) {
            tinos_strlcpy(previous[p].name, rows[p].name,
                          sizeof(previous[p].name));
            tinos_strlcpy(previous[p].icon, rows[p].icon,
                          sizeof(previous[p].icon));
        }
    }

    selected = -1;
    for (unsigned i = 0; i < row_count; i++)
        if (rows[i].info.pid == selected_pid)
            selected = (int)i;
    ui.dirty = true;
}

static void graph(tinos_canvas_t *canvas, int x, int y, int w, int h,
                  const uint8_t *values, const char *title, unsigned now,
                  uint32_t color, const tinos_gui_theme_t *theme)
{
    tinos_canvas_fill(canvas, x, y, w, h, theme->terminal_bg);
    for (int gy = 1; gy < 4; gy++)
        tinos_canvas_fill(canvas, x + 1, y + gy * h / 4, w - 2, 1, 0x1f2a3au);
    for (int gx = 1; gx < 6; gx++)
        tinos_canvas_fill(canvas, x + gx * w / 6, y + 1, 1, h - 2, 0x1f2a3au);
    int inner = h - 2;
    int step = (w - 2) / (HISTORY - 1);
    if (step < 1)
        step = 1;
    int start = x + w - 1 - (int)(history_count ? history_count - 1 : 0) * step;
    for (unsigned i = 0; i < history_count; i++) {
        int bar = values[i] * inner / 100;
        int px = start + (int)i * step;
        if (px < x + 1)
            continue;
        int width = step;
        if (px + width > x + w - 1)
            width = x + w - 1 - px;
        tinos_canvas_fill(canvas, px, y + 1 + inner - bar, width, bar,
                          (color & 0xfefefeu) >> 1);
        tinos_canvas_fill(canvas, px, y + 1 + inner - bar, width,
                          bar ? 1 : 0, color);
    }
    tinos_canvas_frame(canvas, x, y, w, h, theme->border);
    char label[40] = "";
    append(label, sizeof(label), title);
    append(label, sizeof(label), " ");
    append_u32(label, sizeof(label), now);
    append(label, sizeof(label), "%");
    tinos_canvas_text(canvas, x + 6, y + 4, w - 12, label, theme->terminal_text);
}

static int table_top(void)
{
    return HEADER_H + GRAPH_H + 16;
}

static int footer_top(void)
{
    return (int)ui.height - 34;
}

static int end_button_x(void)
{
    return (int)ui.width - BUTTON_W - 12;
}

static void paint(tinos_widget_view_t *view, tinos_canvas_t *canvas,
                  void *context)
{
    (void)context;
    const tinos_gui_theme_t *theme = &view->theme;
    int w = (int)view->width, h = (int)view->height;
    tinos_canvas_fill(canvas, 0, 0, w, h, theme->background);
    tinos_canvas_icon(canvas, "taskmgr", 12, 6, 32);
    tinos_canvas_text(canvas, 52, 8, w - 64, "Task Manager", theme->foreground);
    char summary[64] = "";
    append_u32(summary, sizeof(summary), load.processes);
    append(summary, sizeof(summary), " of ");
    append_u32(summary, sizeof(summary), load.process_max);
    append(summary, sizeof(summary), " process slots in use");
    tinos_canvas_text(canvas, 52, 24, w - 64, summary, theme->muted);

    int gw = (w - 36) / 2;
    graph(canvas, 12, HEADER_H, gw, GRAPH_H, cpu_history, "CPU",
          history_count ? cpu_history[history_count - 1] : 0, 0x6ee7b7u, theme);
    graph(canvas, 24 + gw, HEADER_H, gw, GRAPH_H, mem_history, "Memory",
          history_count ? mem_history[history_count - 1] : 0, 0x80acf0u, theme);

    /* Process table */
    int top = table_top(), bottom = footer_top() - 8;
    int cname = 70, cstate = w - 250, cmem = w - 170, ccpu = w - 70;
    tinos_canvas_fill(canvas, 12, top, w - 24, bottom - top, theme->border);
    tinos_canvas_fill(canvas, 13, top + 1, w - 26, bottom - top - 2,
                      theme->surface);
    tinos_canvas_fill(canvas, 13, top + 1, w - 26, ROW_H, theme->panel);
    tinos_canvas_text(canvas, 20, top + 3, 48, "PID", theme->foreground);
    tinos_canvas_text(canvas, cname, top + 3, cstate - cname, "Name",
                      theme->foreground);
    tinos_canvas_text(canvas, cstate, top + 3, 72, "State", theme->foreground);
    tinos_canvas_text(canvas, cmem, top + 3, 96, "Memory", theme->foreground);
    tinos_canvas_text(canvas, ccpu, top + 3, 56, "CPU", theme->foreground);
    tinos_canvas_fill(canvas, 13, top + ROW_H + 1, w - 26, 1, theme->border);

    int y = top + ROW_H + 3;
    for (unsigned i = 0; i < row_count && y + ROW_H <= bottom; i++, y += ROW_H) {
        const row_t *row = &rows[i];
        bool chosen = (int)i == selected;
        uint32_t ink = chosen ? theme->accent_text : theme->foreground;
        if (chosen)
            tinos_canvas_fill(canvas, 14, y, w - 28, ROW_H, theme->selection);
        char text[24];
        tinos_u32(text, row->info.pid);
        tinos_canvas_text(canvas, 20, y + 2, 48, text, ink);
        tinos_canvas_icon(canvas, row->icon, cname - 20, y + 2, 16);
        tinos_canvas_text(canvas, cname, y + 2, cstate - cname - 8, row->name,
                          ink);
        const char *state = row->info.state == TINOS_PROC_RUNNING ? "running" :
                            row->info.state == TINOS_PROC_BLOCKED ? "waiting" :
                                                                    "ready";
        tinos_canvas_text(canvas, cstate, y + 2, 72, state,
                          chosen ? ink : theme->muted);
        format_kib(text, sizeof(text), row->info.pages * 4);
        tinos_canvas_text(canvas, cmem, y + 2, 96, text, ink);
        text[0] = '\0';
        append_u32(text, sizeof(text), row->cpu_percent);
        append(text, sizeof(text), "%");
        tinos_canvas_text(canvas, ccpu, y + 2, 56, text, ink);
    }
    if (!row_count)
        tinos_canvas_text(canvas, 20, y + 2, w - 40, "No user processes",
                          theme->muted);

    /* Footer: memory summary and End task. */
    int fy = footer_top();
    char used[24], total[24], heap[24];
    format_kib(used, sizeof(used),
               load.memory_total_kib - load.memory_free_kib);
    format_kib(total, sizeof(total), load.memory_total_kib);
    format_kib(heap, sizeof(heap), load.heap_used_kib);
    char line[96] = "RAM ";
    append(line, sizeof(line), used);
    append(line, sizeof(line), " / ");
    append(line, sizeof(line), total);
    append(line, sizeof(line), "  heap ");
    append(line, sizeof(line), heap);
    tinos_canvas_text(canvas, 14, fy + 6, end_button_x() - 24,
                      status_line[0] ? status_line : line,
                      status_line[0] ? theme->danger : theme->muted);
    tinos_canvas_button(canvas, end_button_x(), fy, BUTTON_W, 26, "End task",
                        selected >= 0);
}

static void select_row(int index)
{
    if (index < 0 || index >= (int)row_count)
        return;
    selected = index;
    selected_pid = rows[index].info.pid;
    status_line[0] = '\0';
    ui.dirty = true;
}

static void end_task(void)
{
    if (selected < 0)
        return;
    int pid = (int)rows[selected].info.pid;
    if (pid == tinos_getpid()) {
        tinos_strlcpy(status_line, "Close Task Manager to end it",
                      sizeof(status_line));
    } else if (tinos_terminate(pid) < 0) {
        tinos_strlcpy(status_line, "Could not end that task",
                      sizeof(status_line));
    } else {
        status_line[0] = '\0';
        selected_pid = 0;
    }
    sample();
}

int main(void)
{
    tinos_system_info_t system;
    bool roomy = tinos_sysinfo(&system) == 0 &&
                 system.memory_total_kib >= 12288;
    if (tinos_widget_open(&ui, "Task Manager", roomy ? 520 : 440,
                          roomy ? 400 : 330) < 0)
        return 1;
    ui.on_paint = paint;
    tinos_gui_timer(ui.handle, 1000);
    selected_pid = (uint32_t)tinos_getpid();
    sample();
    tinos_widget_flush(&ui);
    tinos_gui_event_t event;
    while (tinos_gui_event(ui.handle, &event) == 1) {
        if (event.type == TINOS_GUI_CLOSE ||
            (event.type == TINOS_GUI_KEY && event.code == TINOS_KEY_ESCAPE))
            break;
        if (event.type == TINOS_GUI_TIMER) {
            sample();
        } else if (event.type == TINOS_GUI_RESIZE) {
            ui.dirty = true;
        } else if (event.type == TINOS_GUI_KEY) {
            if (event.code == TINOS_KEY_UP)
                select_row(selected <= 0 ? 0 : selected - 1);
            else if (event.code == TINOS_KEY_DOWN)
                select_row(selected + 1);
            else if (event.code == TINOS_KEY_DELETE)
                end_task();
            else if (event.ch == 'r' || event.ch == 'R' ||
                     event.code == TINOS_KEY_F5)
                sample();
        } else if (event.type == TINOS_GUI_MOUSE &&
                   (event.code & TINOS_GUI_MOUSE_LEFT)) {
            int first = table_top() + ROW_H + 3;
            if (event.y >= first && event.y < footer_top() - 8 &&
                event.x >= 12 && event.x < (int)ui.width - 12)
                select_row((event.y - first) / ROW_H);
            else if (event.y >= footer_top() && event.y < footer_top() + 26 &&
                     event.x >= end_button_x() &&
                     event.x < end_button_x() + BUTTON_W)
                end_task();
        }
        if (tinos_widget_flush(&ui) < 0)
            break;
    }
    tinos_widget_close(&ui);
    return 0;
}
