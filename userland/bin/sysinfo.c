#include <tinos/string.h>
#include <tinos/ui.h>

static tinos_ui_t ui;

static void numeric_row(unsigned row, const char *label, uint32_t value,
                        const char *unit)
{
    char number[11];
    tinos_u32(number, value);
    tinos_ui_text(&ui, 2, row, label);
    tinos_ui_text(&ui, 18, row, number);
    tinos_ui_text(&ui, 18 + strlen(number), row, unit);
}

static void show_info(void)
{
    tinos_system_info_t info;
    if (tinos_sysinfo(&info) < 0)
        return;
    tinos_ui_clear(&ui);
    tinos_ui_text(&ui, 2, 1, "System Information");
    tinos_ui_text(&ui, 2, 3, "CPU");
    tinos_ui_text(&ui, 18, 3, info.cpu);
    tinos_ui_text(&ui, 2, 4, "Platform");
    tinos_ui_text(&ui, 18, 4, info.platform);
    tinos_ui_text(&ui, 2, 5, "Clock");
    tinos_ui_text(&ui, 18, 5, info.clock);
    numeric_row(7, "RAM total", info.memory_total_kib, " KiB");
    numeric_row(8, "RAM free", info.memory_free_kib, " KiB");
    numeric_row(9, "Uptime", info.uptime_ms / 1000, " s");
    numeric_row(10, "Display width", info.screen_width, " px");
    numeric_row(11, "Display height", info.screen_height, " px");
    numeric_row(12, "Filesystem nodes", info.fs_nodes, "");
    tinos_ui_text(&ui, 2, ui.rows - 2, "Press R to refresh, Esc to close");
    tinos_ui_present(&ui);
}

int main(void)
{
    if (tinos_ui_open(&ui, "System Information", 540, 320) < 0)
        return 1;
    show_info();
    tinos_gui_event_t event;
    while (tinos_gui_event(ui.handle, &event) == 1) {
        if (event.type == TINOS_GUI_CLOSE ||
            (event.type == TINOS_GUI_KEY &&
             event.code == TINOS_KEY_ESCAPE))
            break;
        if (event.type == TINOS_GUI_KEY &&
            (event.ch == 'r' || event.ch == 'R'))
            show_info();
    }
    tinos_gui_close(ui.handle);
    return 0;
}
