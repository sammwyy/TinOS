/*
 * Interrupt stubs.
 *
 * Every vector pushes a dummy error code (when the CPU does not push one)
 * followed by the vector number, then branches to a common trampoline that
 * builds a regs_t frame and calls the C dispatcher.
 */

.section .text

/* Vectors that do not push an error code. */
.macro ISR_NOERR num
.global isr\num
.type isr\num, @function
isr\num:
    cli
    pushl   $0
    pushl   $\num
    jmp     isr_common
.endm

/* Vectors where the CPU pushes an error code itself. */
.macro ISR_ERR num
.global isr\num
.type isr\num, @function
isr\num:
    cli
    pushl   $\num
    jmp     isr_common
.endm

.macro IRQ num, vector
.global irq\num
.type irq\num, @function
irq\num:
    cli
    pushl   $0
    pushl   $\vector
    jmp     isr_common
.endm

    ISR_NOERR 0     /* divide by zero            */
    ISR_NOERR 1     /* debug                     */
    ISR_NOERR 2     /* non maskable interrupt    */
    ISR_NOERR 3     /* breakpoint                */
    ISR_NOERR 4     /* overflow                  */
    ISR_NOERR 5     /* bound range exceeded      */
    ISR_NOERR 6     /* invalid opcode            */
    ISR_NOERR 7     /* device not available      */
    ISR_ERR   8     /* double fault              */
    ISR_NOERR 9     /* coprocessor overrun       */
    ISR_ERR   10    /* invalid TSS               */
    ISR_ERR   11    /* segment not present       */
    ISR_ERR   12    /* stack segment fault       */
    ISR_ERR   13    /* general protection fault  */
    ISR_ERR   14    /* page fault                */
    ISR_NOERR 15    /* reserved                  */
    ISR_NOERR 16    /* x87 floating point        */
    ISR_ERR   17    /* alignment check           */
    ISR_NOERR 18    /* machine check             */
    ISR_NOERR 19    /* SIMD floating point       */
    ISR_NOERR 20    /* virtualisation            */
    ISR_NOERR 21
    ISR_NOERR 22
    ISR_NOERR 23
    ISR_NOERR 24
    ISR_NOERR 25
    ISR_NOERR 26
    ISR_NOERR 27
    ISR_NOERR 28
    ISR_NOERR 29
    ISR_ERR   30    /* security exception        */
    ISR_NOERR 31
    ISR_NOERR 128   /* userspace syscall */

    IRQ 0,  32
    IRQ 1,  33
    IRQ 2,  34
    IRQ 3,  35
    IRQ 4,  36
    IRQ 5,  37
    IRQ 6,  38
    IRQ 7,  39
    IRQ 8,  40
    IRQ 9,  41
    IRQ 10, 42
    IRQ 11, 43
    IRQ 12, 44
    IRQ 13, 45
    IRQ 14, 46
    IRQ 15, 47

/*
 * Common trampoline.  Stack on entry (low -> high):
 *   [esp+0] vector, [esp+4] error code, then EIP/CS/EFLAGS from the CPU.
 * The layout produced below must match regs_t in <tinos/arch.h>.
 */
.extern isr_dispatch
.type isr_common, @function
isr_common:
    pusha                       /* eax ecx edx ebx esp ebp esi edi */
    movl    %gs, %eax
    pushl   %eax                /* saved TLS segment */
    movl    %ds, %eax
    pushl   %eax                /* saved data segment */
    cld                         /* user code may set DF before a trap */

    movw    $0x10, %ax          /* kernel data segment */
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs

    movl    %esp, %eax
    pushl   %eax                /* regs_t * argument */
    call    isr_dispatch
    addl    $4, %esp

    /* The dispatcher returns the frame selected for iret. */
    testl   %eax, %eax
    jz      1f
    movl    %eax, %esp
1:

    popl    %eax                /* restore data segments */
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    popl    %eax
    movw    %ax, %gs

    popa
    addl    $8, %esp            /* drop vector and error code */
    iret
.size isr_common, . - isr_common
