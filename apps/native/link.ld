/* Links a native app at the start of its reserved slot in the arena
 * described by kernel/include/tinos/native_app.h. Keep these two in sync:
 * a linker script cannot #include the C header that owns the real
 * constants.
 *
 * NATIVE_APP_BASE = 0x01000000, NATIVE_APP_SLOT_SIZE = 0x00100000.
 * calculator is slot 0, so it links at NATIVE_APP_BASE itself; a second
 * native app would link at 0x01100000, a third at 0x01200000, and so on.
 */
ENTRY(app_main)

SECTIONS
{
    . = 0x01000000;

    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note.*) }
}
