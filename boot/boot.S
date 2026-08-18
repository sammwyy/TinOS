/*
 * TinOS - Multiboot2 entry point (i386 protected mode)
 *
 * GRUB2 loads us at 1 MiB already in 32-bit protected mode with paging
 * disabled and a flat (but unknown) GDT.  We request an EGA text-mode
 * framebuffer so that the kernel starts life in 80x25 text mode and performs
 * the switch into graphics mode itself (see kernel/dev/vbe.c).
 */

.set MB2_MAGIC,     0xE85250D6
.set MB2_ARCH_I386, 0

/* Multiboot2 header */
.section .multiboot, "a"
.align 8
mb2_header_start:
    .long   MB2_MAGIC
    .long   MB2_ARCH_I386
    .long   mb2_header_end - mb2_header_start
    .long   -(MB2_MAGIC + MB2_ARCH_I386 + (mb2_header_end - mb2_header_start))

    /* Console flags tag, with no framebuffer tag alongside it.
     *
     * This combination tells GRUB that the kernel drives an EGA text console
     * itself, which makes it set "gfxpayload=text" and hand control over in
     * 80x25 text mode.  Requesting a framebuffer tag here (even with depth 0)
     * would instead make GRUB pick a graphics mode and leave the text console
     * unusable, which is exactly what TinOS does not want: the boot log is
     * printed in text mode and the kernel switches to graphics on its own. */
.align 8
mb2_tag_console_start:
    .short  4                       /* MULTIBOOT_HEADER_TAG_CONSOLE_FLAGS */
    .short  0                       /* flags: required                    */
    .long   mb2_tag_console_end - mb2_tag_console_start
    .long   3                       /* console required | EGA text support */
mb2_tag_console_end:

.align 8
    .short  0                       /* end tag */
    .short  0
    .long   8
mb2_header_end:

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

    /* kmain(uint32_t magic, uint32_t mbi_phys) - cdecl, args right to left */
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
