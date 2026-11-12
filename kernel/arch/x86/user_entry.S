/* Synchronous i386 user entry and return on the boot processor. */
.section .bss
.align 4
user_kernel_esp:
    .long 0

.section .text
.global arch_enter_user
.type arch_enter_user, @function
arch_enter_user:
    pushl %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl %esp, user_kernel_esp
    movl 20(%esp), %eax
    movl 24(%esp), %edx
    pushl $0x23
    pushl %edx
    pushl $0x202
    pushl $0x1b
    pushl %eax
    movw $0x23, %cx
    movw %cx, %ds
    movw %cx, %es
    movw %cx, %fs
    movw %cx, %gs
    xorl %eax, %eax
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorl %esi, %esi
    xorl %edi, %edi
    xorl %ebp, %ebp
    iret
.size arch_enter_user, . - arch_enter_user

.global arch_resume_user
.type arch_resume_user, @function
arch_resume_user:
    pushl %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl %esp, user_kernel_esp
    movl 20(%esp), %esp
    popl %eax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    popa
    addl $8, %esp
    iret
.size arch_resume_user, . - arch_resume_user

.global arch_leave_user
.type arch_leave_user, @function
arch_leave_user:
    cli
    movl 4(%esp), %eax
    movl user_kernel_esp, %esp
    movw $0x10, %cx
    movw %cx, %ds
    movw %cx, %es
    movw %cx, %fs
    movw %cx, %gs
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    ret
.size arch_leave_user, . - arch_leave_user
