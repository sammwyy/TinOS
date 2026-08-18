/* Links an app at the start of its own slot in the arena described by
 * kernel/include/tinos/native_app.h. NATIVE_APP_LOAD_ADDR is not defined
 * here - apps/Makefile passes it per app with --defsym, one distinct slot
 * address per app (NATIVE_APP_BASE + its index * NATIVE_APP_SLOT_SIZE), so
 * every app can share this one script instead of needing its own copy.
 */
ENTRY(app_main)

SECTIONS
{
    . = NATIVE_APP_LOAD_ADDR;

    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note.*) }
}
