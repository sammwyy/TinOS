#include <tinos/libc.h>
#include <tinos/string.h>

int main(void)
{
    tinos_gui_event_t event;
    tinos_dirent_t entry;
    tinos_system_info_t info;
    int ok = tinos_gui_close(1) == -1 &&
             tinos_gui_event(1, &event) == -1 &&
             tinos_gui_present(1, (const char *)0x00100000u, 1, 1) == -1 &&
             tinos_gui_event(1, (tinos_gui_event_t *)0x00100000u) == -1 &&
             tinos_open_flags("/apps/gui/terminal.elf",
                              TINOS_O_WRONLY | TINOS_O_TRUNC) == -1 &&
             tinos_unlink("/apps/gui/terminal.elf") == -1 &&
             tinos_stat("/apps/gui/terminal.elf", &entry) == 1 &&
             tinos_sysinfo((tinos_system_info_t *)0x00100000u) == -1 &&
             tinos_sysinfo(&info) == 0 && info.memory_total_kib > 0 &&
             tinos_ioctl(3, TINOS_IOCTL_KBD_SET_LAYOUT, TINOS_KBD_ES) == -1 &&
             tinos_ioctl(0, TINOS_IOCTL_KBD_SET_LAYOUT, 99) == -1 &&
             tinos_gui_console_attach(1) == -1 &&
             tinos_gui_console_read(1, (char *)0x00100000u, 1) == -1 &&
             tinos_gui_blit(1, (const uint32_t *)0x00100000u, 32, 16) == -1 &&
             tinos_gui_timer(1, 20) == -1 &&
             tinos_tty_send(1, 'x') == -1 &&
             tinos_kill(1) == -1 &&
             tinos_audio_open(1) == -1 &&
             tinos_audio_write(1, (const int16_t *)0x00100000u, 1) == -1 &&
             tinos_audio_control(1, TINOS_AUDIO_CLOSE) == -1;
    const char *message = ok ? "ring3: GUI handles and files isolated\n" :
                               "ring3: GUI isolation FAILED\n";
    tinos_write(1, message, strlen(message));
    return ok ? 0 : 1;
}
