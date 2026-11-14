#ifndef TINOS_PROCESS_H
#define TINOS_PROCESS_H

#include <tinos/kernel.h>
#include <tinos/arch.h>
#include <tinos/input.h>
#include <tinos/types.h>
#include <uapi/tinos/desktop.h>

#define PROCESS_MAX 8
#define PROCESS_INPUT_WAIT (-2)

typedef enum {
    PROCESS_UNUSED,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_EXITED,
} process_state_t;

typedef struct {
    uint32_t pid;
    uint32_t parent_pid;
    process_state_t state;
    int status;
    uint32_t pages;     /* user pages currently mapped */
    uint32_t cpu_ticks; /* PIT ticks spent running, at TIMER_HZ */
    char path[64];      /* argv[0] at spawn */
} process_info_t;

int process_spawn(const void *image, size_t length, int argc,
                  const char *const argv[]);
uint32_t process_current_pid(void);
int process_wait(uint32_t pid, uintptr_t status_address, regs_t *frame);
void process_schedule(void);
size_t process_list(process_info_t *out, size_t capacity);
/* argv[0] of a live process, or NULL. */
const char *process_path(uint32_t pid);
/* Milliseconds of CPU time consumed by processes that have exited. */
uint32_t process_retired_ms(void);
bool process_kill(uint32_t pid);
bool process_reap(uint32_t pid, int *status);
bool process_wants_input(void);
bool process_feed_key(const key_event_t *key);
bool process_cancel_input(void);
int process_read_console(char *ch);
NORETURN void process_block_console(regs_t *frame);
NORETURN void process_block_console_output(regs_t *frame);
NORETURN void process_block_audio(regs_t *frame, uint32_t maximum);
void process_audio_wake(void);
void process_wake_console_output(uint32_t parent_pid);
int process_console_output(const void *data, size_t length);
int process_console_send(uint32_t child_pid, uint8_t ch);
int process_kill_child(uint32_t child_pid);
NORETURN void process_block_gui(regs_t *frame, uintptr_t event_address);
void process_gui_wake(uint32_t pid);
void process_on_tick(regs_t *frame);
void process_maybe_preempt(regs_t *frame);
bool process_active(void);
bool process_user_buffer(uintptr_t address, size_t length, bool writable);
int process_file_open(const char *path, uint32_t flags);
int process_file_read(int fd, void *buffer, size_t length);
int process_file_seek(int fd, uint32_t offset);
int process_file_write(int fd, const void *buffer, size_t length);
int process_file_close(int fd);
int process_file_mkdir(const char *path);
int process_file_unlink(const char *path);
int process_file_transfer(const char *source, const char *destination,
                          bool copy);
int process_library_open(const char *name);
int process_library_close(int handle);
int process_library_prepare_close(int handle);
uintptr_t process_library_symbol(int handle, const char *name);
uintptr_t process_library_versioned_symbol(int handle, const char *name,
                                           const char *version);
uintptr_t process_library_initializer(void);
uintptr_t process_library_finalizer(void);
uintptr_t process_library_close_finalizer(void);
NORETURN void process_abort(int reason);

#endif /* TINOS_PROCESS_H */
