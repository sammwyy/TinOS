#ifndef TINOS_TTY_H
#define TINOS_TTY_H

#include <tinos/types.h>

void tty_start(void);
/* Returns true when the shell requests the graphical desktop. */
bool tty_step(void *context);

#endif /* TINOS_TTY_H */
