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
    leal  4(%esp), %ecx
    movl  %ecx, 16(%eax)
    movl  (%esp), %ecx
    movl  %ecx, 20(%eax)

    movl  0(%edx), %ebx
    movl  4(%edx), %esi
    movl  8(%edx), %edi
    movl  12(%edx), %ebp
    movl  16(%edx), %esp
    jmp   *20(%edx)
.size arch_context_switch, . - arch_context_switch

.global arch_resume_context
.type arch_resume_context, @function
arch_resume_context:
    /* %eax already holds the thread_context_t* to resume: it travels in
     * the synthetic frame's own eax slot (see prepare_cooperative_frame()
     * in kernel/sched/thread.c) and popa, in isr_common's tail, restored it
     * before the iret that landed us here. Per-core by construction - no
     * shared global needed, unlike the single thread_irq_resume_context
     * this replaced, which broke once two cores could each be resuming a
     * different cooperatively-yielded thread through this same path at
     * once (every SMP application processor runs thread_run() in a loop,
     * see smp.c:ap_entry()). */
    movl  0(%eax), %ebx
    movl  4(%eax), %esi
    movl  8(%eax), %edi
    movl  12(%eax), %ebp
    movl  16(%eax), %esp
    sti
    jmp   *20(%eax)
.size arch_resume_context, . - arch_resume_context

.global arch_resume_irq
.type arch_resume_irq, @function
arch_resume_irq:
    movl  (%esp), %eax
    movl  %eax, %esp
    popl  %eax
    movw  %ax, %ds
    movw  %ax, %es
    movw  %ax, %fs
    popl  %eax
    movw  %ax, %gs
    popa
    /* Drop the synthetic vector and error-code words before iret. */
    addl  $8, %esp
    iret
.size arch_resume_irq, . - arch_resume_irq
