#include <tinos/ui.h>

static tinos_ui_t ui;

int main(void)
{
    if (tinos_ui_open(&ui, "About TinOS", 440, 240) < 0)
        return 1;
    tinos_ui_text(&ui, 3, 2, "TinOS 1.0");
    tinos_ui_text(&ui, 3, 4, "An isolated userspace window on i386.");
    tinos_ui_text(&ui, 3, 6, "ELF32  |  ring 3  |  int 0x80");
    tinos_ui_text(&ui, 3, 9, "Press Escape or close the window to exit.");
    tinos_ui_present(&ui);

    tinos_gui_event_t event;
    while (tinos_gui_event(ui.handle, &event) == 1)
        if (event.type == TINOS_GUI_CLOSE ||
            (event.type == TINOS_GUI_KEY && event.code == TINOS_KEY_ESCAPE))
            break;
    tinos_gui_close(ui.handle);
    return 0;
}
