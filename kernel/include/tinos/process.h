#ifndef TINOS_PROCESS_H
#define TINOS_PROCESS_H

#include <tinos/kernel.h>
#include <tinos/types.h>

/* Runs one ELF32 process synchronously; returns its exit status. */
bool process_run(const void *image, size_t length, int *status);
bool process_active(void);
bool process_user_buffer(uintptr_t address, size_t length, bool writable);
NORETURN void process_abort(int reason);

#endif /* TINOS_PROCESS_H */
