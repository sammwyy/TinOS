/* TinOS syscall ABI v1: EAX number, EBX/ECX/EDX arguments, EAX result. */
#ifndef TINOS_UAPI_SYSCALL_H
#define TINOS_UAPI_SYSCALL_H

#define TINOS_SYS_EXIT  1u
#define TINOS_SYS_WRITE 2u
#define TINOS_SYS_READ  3u
#define TINOS_SYS_OPEN  4u
#define TINOS_SYS_CLOSE 5u

/* Kernel returns negative values for errors; console input is nonblocking. */
#define TINOS_ERR_INVALID (-1)
#define TINOS_ERR_AGAIN   (-2)

#endif /* TINOS_UAPI_SYSCALL_H */
