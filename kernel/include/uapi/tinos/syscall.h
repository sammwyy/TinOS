/* TinOS syscall ABI v1: EAX number, EBX/ECX/EDX arguments, EAX result. */
#ifndef TINOS_UAPI_SYSCALL_H
#define TINOS_UAPI_SYSCALL_H

#define TINOS_SYS_EXIT  1u
#define TINOS_SYS_WRITE 2u
#define TINOS_SYS_READ  3u
#define TINOS_SYS_OPEN  4u
#define TINOS_SYS_CLOSE 5u
#define TINOS_SYS_GETPID 6u
#define TINOS_SYS_SPAWN  7u
#define TINOS_SYS_WAIT   8u

/* spawn: EBX path, ECX path length, EDX argc, ESI user argv array.
 * wait: EBX child PID, ECX optional pointer to a 32-bit exit status. */

/* Negative return values indicate syscall errors. */
#define TINOS_ERR_INVALID (-1)

#endif /* TINOS_UAPI_SYSCALL_H */
