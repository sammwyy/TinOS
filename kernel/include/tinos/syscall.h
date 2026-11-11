#ifndef TINOS_SYSCALL_H
#define TINOS_SYSCALL_H

#include <tinos/arch.h>

void syscall_dispatch(regs_t *frame);

#endif /* TINOS_SYSCALL_H */
