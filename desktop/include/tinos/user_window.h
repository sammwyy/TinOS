#ifndef TINOS_USER_WINDOW_H
#define TINOS_USER_WINDOW_H

#include <tinos/types.h>
#include <uapi/tinos/desktop.h>

/* Trusted compositor service. Handles are scoped to the caller's PID. */
void desktop_user_window_init(void);
int desktop_user_window_open(uint32_t pid, const char *title, int width, int height);
int desktop_user_window_present(uint32_t pid, int handle, const char *cells,
                                size_t length, size_t columns);
int desktop_user_window_next(uint32_t pid, int handle, tinos_gui_event_t *event);
int desktop_user_window_close(uint32_t pid, int handle);
void desktop_user_window_release(uint32_t pid);
int desktop_user_window_console_attach(uint32_t pid, int handle);
int desktop_user_window_console_write(uint32_t pid, const char *data,
                                      size_t length);
int desktop_user_window_console_read(uint32_t pid, int handle,
                                     char *buffer, size_t length);
void desktop_user_window_child_exit(uint32_t pid, uint32_t child, int status);
int desktop_user_window_blit(uint32_t pid, int handle, const uint32_t *pixels,
                             uint32_t width, uint32_t height);

#endif
