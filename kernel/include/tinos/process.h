#ifndef TINOS_PROCESS_H
#define TINOS_PROCESS_H

#include <tinos/kernel.h>
#include <tinos/arch.h>
#include <tinos/input.h>
#include <tinos/types.h>
#include <uapi/tinos/desktop.h>

#define PROCESS_MAX 4
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
} process_info_t;

int process_spawn(const void *image, size_t length, int argc,
                  const char *const argv[]);
uint32_t process_current_pid(void);
int process_wait(uint32_t pid, uintptr_t status_address, regs_t *frame);
void process_schedule(void);
size_t process_list(process_info_t *out, size_t capacity);
bool process_kill(uint32_t pid);
bool process_reap(uint32_t pid, int *status);
bool process_wants_input(void);
bool process_feed_key(const key_event_t *key);
bool process_cancel_input(void);
int process_read_console(char *ch);
NORETURN void process_block_console(regs_t *frame);
NORETURN void process_block_gui(regs_t *frame, uintptr_t event_address);
void process_gui_wake(uint32_t pid);
void process_on_tick(regs_t *frame);
void process_maybe_preempt(regs_t *frame);
bool process_active(void);
bool process_user_buffer(uintptr_t address, size_t length, bool writable);
int process_file_open(const char *path, uint32_t flags);
int process_file_read(int fd, void *buffer, size_t length);
int process_file_write(int fd, const void *buffer, size_t length);
int process_file_close(int fd);
int process_file_mkdir(const char *path);
int process_file_unlink(const char *path);
NORETURN void process_abort(int reason);

#endif /* TINOS_PROCESS_H */
