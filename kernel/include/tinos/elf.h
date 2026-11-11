/* Minimal 32-bit ELF loader.
 *
 * Loads PT_LOAD segments at their literal p_vaddr - correct only because
 * paging identity-maps the whole address space and every loadable image is
 * linked (non-PIE, no relocations) for a fixed address the caller reserved
 * ahead of time. There is no relocation processing here.
 */
#ifndef TINOS_ELF_H
#define TINOS_ELF_H

#include <tinos/types.h>

/* Copies image's PT_LOAD segments into [region_base, region_base + region_size)
 * and writes the entry point to *entry_out. Every segment must fall inside
 * that region; anything else (bad magic, wrong class/machine, PIE image, a
 * segment reaching outside the region) fails the load instead of touching
 * memory outside it. */
bool elf_load(const void *image, size_t image_size, uintptr_t region_base,
              size_t region_size, void **entry_out);

/* Read the entry address of a supported ELF32 executable. */
bool elf_entry_address(const void *image, size_t image_size,
                       uintptr_t *address_out);

#endif /* TINOS_ELF_H */
