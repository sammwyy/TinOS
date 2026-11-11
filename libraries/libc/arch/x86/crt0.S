/* Entry stack: argc, argv[0..argc-1], NULL. */
#include <tinos/syscall.h>

.section .text
.global _start
.type _start, @function
_start:
    movl (%esp), %eax
    leal 4(%esp), %ebx
    andl $-16, %esp
    subl $8, %esp
    pushl %ebx
    pushl %eax
    call main
    movl %eax, %ebx
    movl $TINOS_SYS_EXIT, %eax
    int $0x80
1:  jmp 1b
.size _start, . - _start
