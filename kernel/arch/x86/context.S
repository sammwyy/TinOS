/* Switch callee-saved registers and stack between two kernel contexts. */

.section .text
.global arch_context_switch
.type arch_context_switch, @function
arch_context_switch:
    /* context layout: ebx, esi, edi, ebp, esp, eip */
    movl  4(%esp), %eax
    movl  8(%esp), %edx
    movl  %ebx, 0(%eax)
    movl  %esi, 4(%eax)
    movl  %edi, 8(%eax)
    movl  %ebp, 12(%eax)
    movl  %esp, 16(%eax)
    movl  (%esp), %ecx
    movl  %ecx, 20(%eax)

    movl  0(%edx), %ebx
    movl  4(%edx), %esi
    movl  8(%edx), %edi
    movl  12(%edx), %ebp
    movl  16(%edx), %esp
    jmp   *20(%edx)
.size arch_context_switch, . - arch_context_switch
