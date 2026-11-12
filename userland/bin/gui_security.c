#include <tinos/libc.h>
#include <tinos/string.h>

int main(void)
{
    tinos_gui_event_t event;
    tinos_dirent_t entry;
    int ok = tinos_gui_close(1) == -1 &&
             tinos_gui_event(1, &event) == -1 &&
             tinos_gui_present(1, (const char *)0x00100000u, 1, 1) == -1 &&
             tinos_gui_event(1, (tinos_gui_event_t *)0x00100000u) == -1 &&
             tinos_open_flags("/apps/gui/terminal.elf",
                              TINOS_O_WRONLY | TINOS_O_TRUNC) == -1 &&
             tinos_unlink("/apps/gui/terminal.elf") == -1 &&
             tinos_stat("/apps/gui/terminal.elf", &entry) == 1;
    const char *message = ok ? "ring3: GUI handles and files isolated\n" :
                               "ring3: GUI isolation FAILED\n";
    tinos_write(1, message, strlen(message));
    return ok ? 0 : 1;
}
