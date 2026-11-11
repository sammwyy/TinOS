/*
 * TinOS kernel linker script.
 *
 * The kernel is linked at 1 MiB for the TinOS BIOS bootloader. Paging is
 * disabled at entry, so link addresses are physical addresses.
 */

ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)

/* AP_TRAMPOLINE_ADDR must be page-aligned and below 1 MiB: it is where
 * kernel/arch/x86/trampoline.S runs in 16-bit real mode when an SMP
 * application processor starts, and its SIPI vector is this address >> 12.
 * 0x8000 is outside the bootloader's active code after handoff; pmm.c
 * reserves the entire first 1 MiB from the frame allocator. */
AP_TRAMPOLINE_ADDR = 0x8000;

SECTIONS
{
    .trampoline AP_TRAMPOLINE_ADDR : AT(AP_TRAMPOLINE_ADDR)
    {
        *(.trampoline)
    }
    AP_TRAMPOLINE_SIZE = SIZEOF(.trampoline);

    . = 1M;
    __kernel_start = .;

    .text BLOCK(4K) : ALIGN(4K)
    {
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
