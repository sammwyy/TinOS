/*
 * AP (application processor) real-mode entry point.
 *
 * Every core other than the boot processor starts executing here, in
 * 16-bit real mode, at CS:IP = (SIPI vector byte) << 8 : 0x0000. This
 * section is linked *and loaded* at a fixed low physical address (see
 * boot/linker.ld), so every address baked into it below is already
 * correct with no runtime relocation - including ap_gdt_ptr_copy, which
 * kernel/arch/x86/smp.c fills in with the boot processor's real (high,
 * ~1 MiB+) GDT base/limit before sending the SIPI: 16-bit real-mode
 * addressing with DS=0 can only reach the first 64 KiB, which is exactly
 * where this whole trampoline (code and this pointer) lives.
 */

.section .trampoline, "ax"
.code16
.global ap_trampoline_start
.global ap_trampoline_end
.global ap_gdt_ptr_copy

ap_trampoline_start:
    cli
    cld
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %ss

    lgdtl   ap_gdt_ptr_copy

    movl    %cr0, %eax
    orl     $1, %eax          /* PE: protected mode */
    movl    %eax, %cr0

    /* "data32" forces a 32-bit-offset far jump even while still assembled
     * as 16-bit code, since the target is linked well past the 64 KiB a
     * 16-bit offset could reach. */
    data32 ljmp $0x08, $ap_protected32

.align 4
ap_gdt_ptr_copy:
    .word   0                 /* limit  - smp.c overwrites both fields   */
    .long   0                 /* base     before sending the SIPI        */

.code32
ap_protected32:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss

    /* smp.c writes this AP's private stack top before sending its SIPI;
     * APs are brought up one at a time, so there is no race on it. */
    movl    ap_stack_top, %esp
    movl    $0, %ebp

    call    ap_entry           /* kernel/arch/x86/smp.c - never returns */

.Lap_halt:
    cli
    hlt
    jmp     .Lap_halt

ap_trampoline_end:
