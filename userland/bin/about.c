#include <tinos/widgets.h>

static tinos_widget_view_t ui;
static bool closing;

static void on_action(tinos_widget_view_t *view, unsigned id, void *context)
{
    (void)view;
    (void)context;
    if (id == 1)
        closing = true;
}

int main(void)
{
    if (tinos_widget_open(&ui, "About TinOS", 440, 240) < 0)
        return 1;
    ui.on_action = on_action;
    int banner = tinos_widget_add(&ui, 0, TINOS_WIDGET_BANNER, 0,
                                  "TINOS / SYSTEM", 0, 32, 0);
    int panel = tinos_widget_add(&ui, 0, TINOS_WIDGET_PANEL, 0, NULL,
                                 0, 112, 1);
    if (banner < 0 || panel < 0 ||
        tinos_widget_add(&ui, panel, TINOS_WIDGET_LABEL, 0,
                         "A small OS with isolated apps.", 0, 24, 0) < 0 ||
        tinos_widget_add(&ui, panel, TINOS_WIDGET_LABEL, 0,
                         "ELF32   |   ring 3   |   i386", 0, 24, 0) < 0 ||
        tinos_widget_add(&ui, 0, TINOS_WIDGET_BUTTON, 1,
                         "Close", 100, 32, 0) < 0 ||
        tinos_widget_flush(&ui) < 0) {
        tinos_widget_close(&ui);
        return 1;
    }

    tinos_gui_event_t event;
    while (!closing && tinos_gui_event(ui.handle, &event) == 1) {
        if (event.type == TINOS_GUI_CLOSE ||
            (event.type == TINOS_GUI_KEY && event.code == TINOS_KEY_ESCAPE))
            break;
        tinos_widget_event(&ui, &event);
        if (!closing && tinos_widget_flush(&ui) < 0)
            break;
    }
    tinos_widget_close(&ui);
    return 0;
}
