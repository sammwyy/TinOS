#include <tinos/elf.h>
#include <tinos/kernel.h>
#include <tinos/string.h>
#ifndef TINOS_HOST
#include <tinos/user_vm.h>
#endif

#define EI_NIDENT   16
#define ELFCLASS32  1
#define ELFDATA2LSB 1
#define ET_EXEC     2
#define EM_386      3
#define PT_LOAD     1
#define PF_X        1u
#define PF_W        2u

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
    if (region_size > (uintptr_t)-1 - region_base)
        return false;
    uintptr_t remaining = region_size - (start - region_base);
    return start - region_base < region_size && length <= remaining;
}

bool elf_entry_address(const void *image, size_t image_size,
                       uintptr_t *address_out)
{
    if (!image || !address_out || image_size < sizeof(elf32_ehdr_t))
        return false;

    const elf32_ehdr_t *ehdr = image;
    static const uint8_t magic[4] = { 0x7f, 'E', 'L', 'F' };
    if (memcmp(ehdr->e_ident, magic, sizeof(magic)) != 0)
        return false;
    if (ehdr->e_ident[4] != ELFCLASS32 || ehdr->e_ident[5] != ELFDATA2LSB)
        return false;
    if (ehdr->e_type != ET_EXEC || ehdr->e_machine != EM_386)
        return false;
    *address_out = ehdr->e_entry;
    return true;
}

bool elf_load(const void *image, size_t image_size, uintptr_t region_base,
             size_t region_size, void **entry_out)
{
    uintptr_t address;
    if (!entry_out || !elf_entry_address(image, image_size, &address))
        return false;
    const elf32_ehdr_t *ehdr = image;
    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0 ||
        ehdr->e_phentsize < sizeof(elf32_phdr_t))
        return false;
    if (!region_contains(region_base, region_size, address, 1))
        return false;

    if (ehdr->e_phoff > image_size ||
        ehdr->e_phnum > (image_size - ehdr->e_phoff) / ehdr->e_phentsize)
        return false;

    bool entry_in_segment = false;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        size_t phdr_offset = ehdr->e_phoff + (size_t)i * ehdr->e_phentsize;
        const elf32_phdr_t *ph =
            (const elf32_phdr_t *)((const uint8_t *)image + phdr_offset);
        if (ph->p_type != PT_LOAD ||
            (ph->p_memsz == 0 && ph->p_filesz == 0))
            continue;
        if (ph->p_filesz > ph->p_memsz)
            return false;
        if (ph->p_offset > image_size ||
            ph->p_filesz > image_size - ph->p_offset)
            return false;
        if (!region_contains(region_base, region_size, ph->p_vaddr,
                             ph->p_memsz))
            return false;
        if (address >= ph->p_vaddr && address - ph->p_vaddr < ph->p_memsz)
            entry_in_segment = true;
    }
    if (!entry_in_segment)
        return false;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        size_t phdr_offset = ehdr->e_phoff + (size_t)i * ehdr->e_phentsize;
        const elf32_phdr_t *ph =
            (const elf32_phdr_t *)((const uint8_t *)image + phdr_offset);
        if (ph->p_type != PT_LOAD ||
            (ph->p_memsz == 0 && ph->p_filesz == 0))
            continue;
        void *dst = (void *)(uintptr_t)ph->p_vaddr;
        memcpy(dst, (const uint8_t *)image + ph->p_offset, ph->p_filesz);
        memset((uint8_t *)dst + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
    }

    *entry_out = (void *)address;
    return true;
}

#ifndef TINOS_HOST
bool elf_user_load(const void *image, size_t image_size,
                   bool (*map)(void *ctx, uintptr_t page), void *ctx,
                   void (*protect)(void *ctx, uintptr_t page),
                   void **entry_out)
{
    uintptr_t entry;
    if (!image || !map || !protect || !entry_out ||
        !elf_entry_address(image, image_size, &entry))
        return false;
    const elf32_ehdr_t *ehdr = image;
    if (ehdr->e_phentsize != sizeof(elf32_phdr_t) || !ehdr->e_phnum ||
        ehdr->e_phoff > image_size ||
        ehdr->e_phnum > (image_size - ehdr->e_phoff) / sizeof(elf32_phdr_t))
        return false;

    const elf32_phdr_t *phdrs =
        (const elf32_phdr_t *)((const uint8_t *)image + ehdr->e_phoff);
    bool executable_entry = false;
    for (size_t i = 0; i < ehdr->e_phnum; i++) {
        const elf32_phdr_t *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD ||
            (ph->p_memsz == 0 && ph->p_filesz == 0))
            continue;
        if (!ph->p_memsz || ph->p_filesz > ph->p_memsz ||
            ph->p_offset > image_size ||
            ph->p_filesz > image_size - ph->p_offset ||
            !region_contains(USER_BASE, 0x100000u, ph->p_vaddr, ph->p_memsz) ||
            ((ph->p_flags & (PF_X | PF_W)) == (PF_X | PF_W)))
            return false;
        uintptr_t first = ph->p_vaddr & ~0xfffu;
        uintptr_t last = (ph->p_vaddr + ph->p_memsz - 1) & ~0xfffu;
        if ((ph->p_flags & PF_X) && entry >= ph->p_vaddr &&
            entry - ph->p_vaddr < ph->p_memsz)
            executable_entry = true;
        for (size_t j = 0; j < i; j++) {
            const elf32_phdr_t *other = &phdrs[j];
            if (other->p_type != PT_LOAD ||
                (other->p_memsz == 0 && other->p_filesz == 0))
                continue;
            uintptr_t a = other->p_vaddr & ~0xfffu;
            uintptr_t b = (other->p_vaddr + other->p_memsz - 1) & ~0xfffu;
            if (a <= last && first <= b)
                return false;
        }
    }
    if (!executable_entry)
        return false;

    for (size_t i = 0; i < ehdr->e_phnum; i++) {
        const elf32_phdr_t *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD ||
            (ph->p_memsz == 0 && ph->p_filesz == 0))
            continue;
        uintptr_t first = ph->p_vaddr & ~0xfffu;
        uintptr_t last = (ph->p_vaddr + ph->p_memsz - 1) & ~0xfffu;
        for (uintptr_t page = first; page <= last; page += 4096)
            if (!map(ctx, page))
                return false;
    }
    if (!elf_load(image, image_size, USER_BASE, 0x100000u, entry_out))
        return false;
    for (size_t i = 0; i < ehdr->e_phnum; i++) {
        const elf32_phdr_t *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD || !ph->p_memsz || (ph->p_flags & PF_W))
            continue;
        uintptr_t first = ph->p_vaddr & ~0xfffu;
        uintptr_t last = (ph->p_vaddr + ph->p_memsz - 1) & ~0xfffu;
        for (uintptr_t page = first; page <= last; page += 4096)
            protect(ctx, page);
    }
    return true;
}
#endif
