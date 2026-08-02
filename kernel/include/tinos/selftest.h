/* In-kernel self tests, run when the kernel command line contains
 * "selftest".  Results are printed to the serial log. */
#ifndef TINOS_SELFTEST_H
#define TINOS_SELFTEST_H

#include <tinos/types.h>

/* Returns the number of failed checks (0 on success). */
int selftest_run(void);

#endif /* TINOS_SELFTEST_H */
