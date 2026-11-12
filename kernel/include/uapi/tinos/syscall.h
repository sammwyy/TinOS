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
#define TINOS_SYS_READDIR 9u
#define TINOS_SYS_STAT   10u
#define TINOS_SYS_GUI_OPEN 11u
#define TINOS_SYS_GUI_PRESENT 12u
#define TINOS_SYS_GUI_EVENT 13u
#define TINOS_SYS_GUI_CLOSE 14u
#define TINOS_SYS_MKDIR 15u
#define TINOS_SYS_UNLINK 16u
#define TINOS_SYS_SYSINFO 17u
#define TINOS_SYS_IOCTL  18u
#define TINOS_SYS_GUI_CONSOLE_ATTACH 19u
#define TINOS_SYS_GUI_CONSOLE_READ 20u
#define TINOS_SYS_TTY_SEND 21u
#define TINOS_SYS_KILL 22u
#define TINOS_SYS_GUI_BLIT 23u
#define TINOS_SYS_GUI_TIMER 24u
#define TINOS_SYS_AUDIO_OPEN 25u
#define TINOS_SYS_AUDIO_WRITE 26u
#define TINOS_SYS_AUDIO_CONTROL 27u
#define TINOS_SYS_DLOPEN 28u
#define TINOS_SYS_DLSYM 29u
#define TINOS_SYS_PCM_OPEN 30u
#define TINOS_SYS_PCM_WAIT 31u

#define TINOS_IOCTL_KBD_GET_LAYOUT 0x4B01u
#define TINOS_IOCTL_KBD_SET_LAYOUT 0x4B02u
#define TINOS_KBD_US 0u
#define TINOS_KBD_ES 1u

/* i386 Linux-compatible open flag values for the implemented subset. */
#define TINOS_O_RDONLY 0u
#define TINOS_O_WRONLY 1u
#define TINOS_O_RDWR   2u
#define TINOS_O_CREAT  0x40u
#define TINOS_O_TRUNC  0x200u
#define TINOS_O_APPEND 0x400u

/* spawn: EBX path, ECX path length, EDX argc, ESI user argv array.
 * wait: EBX child PID, ECX optional pointer to a 32-bit exit status. */

/* Negative return values indicate syscall errors. */
#define TINOS_ERR_INVALID (-1)

#endif /* TINOS_UAPI_SYSCALL_H */
