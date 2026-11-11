/*
 * TinOS - i386 protected-mode entry point.
 *
 * The BIOS loader loads the image at 1 MiB and passes its boot_info address
 * in EBX, with TINOS_BOOT_MAGIC in EAX. Paging is disabled.
 */

/* Boot stack */
.section .bss, "aw", @nobits
.align 16
    .global boot_stack_bottom
boot_stack_bottom:
    .skip 65536                     /* 64 KiB kernel stack */
    .global boot_stack_top
boot_stack_top:

/* Entry point */
.section .text
.global _start
.type _start, @function
_start:
    cli
    cld
    movl    $boot_stack_top, %esp
    xorl    %ebp, %ebp                  /* terminate stack traces here */

    /* The loader keeps the AP trampoline in the low-memory bounce area
     * until execution has left the El Torito boot sector at 0x7c00. */
    pushl   %eax
    pushl   %ebx
    movl    $0x10000, %esi
    movl    $ap_trampoline_start, %edi
    movl    $AP_TRAMPOLINE_SIZE, %ecx
    rep movsb
    popl    %ebx
    popl    %eax

    /* kmain(uint32_t magic, uint32_t boot_info_phys) */
    pushl   %ebx
    pushl   %eax
    call    kmain

    /* kmain must never return; if it does, park the CPU visibly halted. */
    addl    $8, %esp
.Lhalt:
    cli
    hlt
    jmp     .Lhalt
.size _start, . - _start
