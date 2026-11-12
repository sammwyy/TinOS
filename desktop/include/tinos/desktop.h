/* Desktop environment entry point. */
#ifndef TINOS_DESKTOP_H
#define TINOS_DESKTOP_H

#include <tinos/kernel.h>
#include <tinos/types.h>

void desktop_start(void);
bool desktop_step(void *context);

/* Paints a farewell screen and then resets or powers off the machine. */
NORETURN void desktop_shutdown(bool reboot);

#endif /* TINOS_DESKTOP_H */
