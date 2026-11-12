/* Entry stack: argc, argv[0..argc-1], NULL. */
#include <tinos/syscall.h>

.section .text
.global _start
.type _start, @function
_start:
1:  movl $TINOS_SYS_DL_INIT_NEXT, %eax
    int $0x80
    testl %eax, %eax
    jz 2f
    call *%eax
    jmp 1b
2:
    movl (%esp), %eax
    leal 4(%esp), %ebx
    andl $-16, %esp
    subl $8, %esp
    pushl %ebx
    pushl %eax
    call main
    pushl %eax
3:  movl $TINOS_SYS_DL_FINI_NEXT, %eax
    int $0x80
    testl %eax, %eax
    jz 4f
    call *%eax
    jmp 3b
4:  popl %ebx
    movl $TINOS_SYS_EXIT, %eax
    int $0x80
1:  jmp 1b
.size _start, . - _start
