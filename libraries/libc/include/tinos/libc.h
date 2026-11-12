/* Minimal userspace I/O API. No kernel-private types cross this boundary. */
#ifndef TINOS_LIBC_H
#define TINOS_LIBC_H

#include <tinos/desktop.h>
#include <tinos/audio.h>
#include <tinos/syscall.h>

int tinos_open(const char *path);
int tinos_open_flags(const char *path, unsigned flags);
int tinos_mkdir(const char *path);
int tinos_unlink(const char *path);
int tinos_close(int fd);
int tinos_read(int fd, void *buffer, unsigned length);
int tinos_write(int fd, const void *buffer, unsigned length);
int tinos_spawn(const char *path, unsigned argc, const char *const argv[]);
int tinos_wait(int pid, int *status);
int tinos_getpid(void);
int tinos_readdir(const char *path, unsigned index, tinos_dirent_t *entry);
int tinos_stat(const char *path, tinos_dirent_t *entry);
int tinos_sysinfo(tinos_system_info_t *info);
int tinos_ioctl(int fd, unsigned request, unsigned value);
int tinos_gui_open(const char *title, unsigned width, unsigned height);
int tinos_gui_present(int handle, const char *cells, unsigned columns,
                      unsigned rows);
int tinos_gui_event(int handle, tinos_gui_event_t *event);
int tinos_gui_close(int handle);
int tinos_gui_blit(int handle, const uint32_t *pixels,
                   unsigned width, unsigned height);
int tinos_gui_timer(int handle, unsigned interval_ms);
int tinos_audio_open(unsigned sample_rate);
int tinos_audio_write(int handle, const int16_t *samples, unsigned frames);
int tinos_audio_control(int handle, unsigned command);
int tinos_gui_console_attach(int handle);
int tinos_gui_console_read(int handle, char *buffer, unsigned length);
int tinos_tty_send(int child_pid, char ch);
int tinos_kill(int child_pid);
__attribute__((noreturn)) void tinos_exit(int status);

#endif /* TINOS_LIBC_H */
