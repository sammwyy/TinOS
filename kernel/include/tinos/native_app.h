/* Layout of the reserved arena native apps get loaded into.
 *
 * Paging identity-maps the whole address space, so an ELF built with a
 * fixed link address can be copied straight to that physical address and
 * run in place - no per-process page tables, no relocations. Every native
 * app's own linker script hardcodes its slot's base address to match this
 * table; kernel/arch/x86/pmm.c reserves the whole arena before the heap
 * starts handing out frames so nothing else ever lands here.
 */
#ifndef TINOS_NATIVE_APP_H
#define TINOS_NATIVE_APP_H

#include <tinos/types.h>

#define NATIVE_APP_BASE       0x00800000u /* 8 MiB */
#define NATIVE_APP_SLOT_SIZE  0x00020000u /* 128 KiB per app */
#define NATIVE_APP_MAX_COUNT  12u /* 9 in use, 3 free slots */
#define NATIVE_APP_ARENA_SIZE (NATIVE_APP_SLOT_SIZE * NATIVE_APP_MAX_COUNT)

#endif /* TINOS_NATIVE_APP_H */
