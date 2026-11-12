#include <tinos/string.h>
#include <tinos/ui.h>

static tinos_ui_t ui;

static void show_settings(void)
{
    tinos_system_info_t info;
    if (tinos_sysinfo(&info) < 0)
        return;
    tinos_ui_clear(&ui);
    tinos_ui_text(&ui, 2, 1, "Control Panel");
    tinos_ui_text(&ui, 2, 3, "Keyboard layout");
    int layout = tinos_ioctl(0, TINOS_IOCTL_KBD_GET_LAYOUT, 0);
    tinos_ui_text(&ui, 2, 4, layout == TINOS_KBD_ES ?
                  "Spanish (ES)" : "English (US)");
    tinos_ui_text(&ui, 2, 6, "Press 1 for US or 2 for ES");
    tinos_ui_text(&ui, 2, 8, "Display (read-only)");
    char number[11];
    tinos_u32(number, info.screen_width);
    tinos_ui_text(&ui, 2, 9, number);
    unsigned next = 2 + strlen(number);
    tinos_ui_text(&ui, next, 9, " x ");
    tinos_u32(number, info.screen_height);
    tinos_ui_text(&ui, next + 3, 9, number);
    tinos_ui_text(&ui, 2, ui.rows - 2, "Esc closes this window");
    tinos_ui_present(&ui);
}

int main(void)
{
    if (tinos_ui_open(&ui, "Control Panel", 440, 280) < 0)
        return 1;
    show_settings();
    tinos_gui_event_t event;
    while (tinos_gui_event(ui.handle, &event) == 1) {
        if (event.type == TINOS_GUI_CLOSE ||
            (event.type == TINOS_GUI_KEY &&
             event.code == TINOS_KEY_ESCAPE))
            break;
        if (event.type == TINOS_GUI_KEY &&
            (event.ch == '1' || event.ch == '2')) {
            tinos_ioctl(0, TINOS_IOCTL_KBD_SET_LAYOUT,
                        event.ch == '1' ? TINOS_KBD_US : TINOS_KBD_ES);
            show_settings();
        }
    }
    tinos_gui_close(ui.handle);
    return 0;
}
