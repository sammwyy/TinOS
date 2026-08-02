/*
 * TinOS kernel linker script.
 *
 * The kernel is a flat 32-bit ELF loaded by GRUB at the traditional 1 MiB
 * mark.  No paging is used, so link addresses are physical addresses.
 */

ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)

SECTIONS
{
    . = 1M;
    __kernel_start = .;

    .text BLOCK(4K) : ALIGN(4K)
    {
        *(.multiboot)       /* must live in the first 32 KiB of the image */
        *(.text .text.*)
    }

    .rodata BLOCK(4K) : ALIGN(4K)
    {
        *(.rodata .rodata.*)
    }

    .data BLOCK(4K) : ALIGN(4K)
    {
        *(.data .data.*)
    }

    .bss BLOCK(4K) : ALIGN(4K)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        __bss_end = .;
    }

    . = ALIGN(4K);
    __kernel_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note .note.*)
    }
}
