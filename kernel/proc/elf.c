#include <tinos/elf.h>
#include <tinos/kernel.h>
#include <tinos/string.h>

#define EI_NIDENT   16
#define ELFCLASS32  1
#define ELFDATA2LSB 1
#define ET_EXEC     2
#define EM_386      3
#define PT_LOAD     1

typedef struct PACKED {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf32_ehdr_t;

typedef struct PACKED {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} elf32_phdr_t;

static bool region_contains(uintptr_t region_base, size_t region_size,
                            uintptr_t start, size_t length)
{
    if (start < region_base)
        return false;
    uintptr_t region_end = region_base + region_size;
    uintptr_t end;
    if (__builtin_add_overflow(start, length, &end))
        return false;
    return end <= region_end;
}

bool elf_load(const void *image, size_t image_size, uintptr_t region_base,
             size_t region_size, void **entry_out)
{
    if (image_size < sizeof(elf32_ehdr_t))
        return false;

    const elf32_ehdr_t *ehdr = image;
    static const uint8_t magic[4] = { 0x7f, 'E', 'L', 'F' };
    if (memcmp(ehdr->e_ident, magic, sizeof(magic)) != 0)
        return false;
    if (ehdr->e_ident[4] != ELFCLASS32 || ehdr->e_ident[5] != ELFDATA2LSB)
        return false;
    if (ehdr->e_type != ET_EXEC || ehdr->e_machine != EM_386)
        return false;
    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0 ||
        ehdr->e_phentsize < sizeof(elf32_phdr_t))
        return false;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        size_t phdr_offset = ehdr->e_phoff + (size_t)i * ehdr->e_phentsize;
        if (phdr_offset + sizeof(elf32_phdr_t) > image_size)
            return false;
        const elf32_phdr_t *ph =
            (const elf32_phdr_t *)((const uint8_t *)image + phdr_offset);
        if (ph->p_type != PT_LOAD)
            continue;
        if (ph->p_filesz > ph->p_memsz)
            return false;
        if (ph->p_offset + ph->p_filesz > image_size)
            return false;
        if (!region_contains(region_base, region_size, ph->p_vaddr,
                             ph->p_memsz))
            return false;

        void *dst = (void *)(uintptr_t)ph->p_vaddr;
        memcpy(dst, (const uint8_t *)image + ph->p_offset, ph->p_filesz);
        memset((uint8_t *)dst + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
    }

    if (!region_contains(region_base, region_size, ehdr->e_entry, 0))
        return false;

    *entry_out = (void *)(uintptr_t)ehdr->e_entry;
    return true;
}
