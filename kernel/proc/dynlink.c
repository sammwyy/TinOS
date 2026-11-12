#include <tinos/dynlink.h>
#include <tinos/fs.h>
#include <tinos/kernel.h>
#include <tinos/mm.h>
#include <tinos/string.h>

#define LIB_REGION_SIZE 0x10000u
#define LIB_PAGES (LIB_REGION_SIZE / PAGE_SIZE)
#define LIB_LIMIT DYNLINK_LIBRARY_LIMIT
#define ELF_LOAD 1u
#define ELF_DYNAMIC 2u
#define ELF_INTERP 3u
#define ELF_TLS 7u
#define ELF_EXEC 2u
#define ELF_DYN 3u
#define ELF_MACHINE_386 3u
#define ELF_WRITE 2u
#define ELF_EXECUTE 1u
#define DT_NULL 0
#define DT_NEEDED 1
#define DT_PLTRELSZ 2
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_INIT 12
#define DT_FINI 13
#define DT_STRSZ 10
#define DT_SYMENT 11
#define DT_REL 17
#define DT_RELSZ 18
#define DT_RELENT 19
#define DT_PLTREL 20
#define DT_TEXTREL 22
#define DT_JMPREL 23
#define DT_INIT_ARRAY 25
#define DT_FINI_ARRAY 26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_PREINIT_ARRAY 32
#define DT_PREINIT_ARRAYSZ 33
#define DT_GNU_HASH 0x6ffffef5
#define DT_VERSYM 0x6ffffff0
#define DT_VERDEF 0x6ffffffc
#define DT_VERDEFNUM 0x6ffffffd
#define DT_VERNEED 0x6ffffffe
#define DT_VERNEEDNUM 0x6fffffff
#define R_386_32 1u
#define R_386_GLOB_DAT 6u
#define R_386_JUMP_SLOT 7u
#define R_386_RELATIVE 8u

typedef struct PACKED {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} elf_header_t;

typedef struct PACKED {
    uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
} program_header_t;

typedef struct PACKED {
    int32_t tag;
    uint32_t value;
} dynamic_entry_t;

typedef struct PACKED {
    uint32_t name, value, size;
    uint8_t info, other;
    uint16_t section;
} symbol_t;

typedef struct PACKED {
    uint32_t offset, info;
} relocation_t;

typedef struct {
    char name[FS_NAME_MAX];
    uintptr_t base;
    void *frames[LIB_PAGES];
    bool ready;
} shared_library_t;

static shared_library_t libraries[LIB_LIMIT];

static bool valid_image(const void *image, size_t size, uint16_t type,
                        const elf_header_t **header_out)
{
    if (!image || size < sizeof(elf_header_t))
        return false;
    const elf_header_t *header = image;
    if (memcmp(header->ident, "\177ELF", 4) ||
        header->ident[4] != 1 || header->ident[5] != 1 ||
        header->ident[6] != 1 || header->version != 1 ||
        header->ehsize != sizeof(*header) ||
        header->type != type || header->machine != ELF_MACHINE_386 ||
        header->phentsize != sizeof(program_header_t) ||
        !header->phnum || header->phnum > 16 ||
        header->phoff > size ||
        header->phnum > (size - header->phoff) / sizeof(program_header_t))
        return false;
    *header_out = header;
    return true;
}

static const program_header_t *programs(const void *image,
                                        const elf_header_t *header)
{
    return (const program_header_t *)((const uint8_t *)image + header->phoff);
}

static bool mapped(const user_space_t *space, uintptr_t address,
                   size_t size, bool writable)
{
    if (!size || address < USER_BASE || address >= USER_REGION_END ||
        size > USER_REGION_END - address)
        return false;
    uintptr_t last = address + size - 1;
    for (uintptr_t page = address & ~(uintptr_t)(PAGE_SIZE - 1);
         page <= last; page += PAGE_SIZE) {
        uint32_t pte = space->table[(page - USER_BASE) / PAGE_SIZE];
        if ((pte & (writable ? 7u : 5u)) != (writable ? 7u : 5u))
            return false;
    }
    return true;
}

static bool shared_segments_valid(const void *image, size_t size,
                                  const elf_header_t *header)
{
    const program_header_t *ph = programs(image, header);
    unsigned pages = 0;
    for (unsigned i = 0; i < header->phnum; i++) {
        if (ph[i].type == ELF_INTERP || ph[i].type == ELF_TLS)
            return false;
        if (ph[i].type != ELF_LOAD || !ph[i].memsz)
            continue;
        if (ph[i].filesz > ph[i].memsz ||
            ph[i].offset > size || ph[i].filesz > size - ph[i].offset ||
            ph[i].vaddr >= LIB_REGION_SIZE ||
            ph[i].memsz > LIB_REGION_SIZE - ph[i].vaddr ||
            (ph[i].flags & (ELF_WRITE | ELF_EXECUTE)) ==
                (ELF_WRITE | ELF_EXECUTE) ||
            (ph[i].offset & (PAGE_SIZE - 1)) !=
                (ph[i].vaddr & (PAGE_SIZE - 1)))
            return false;
        uintptr_t first = ph[i].vaddr / PAGE_SIZE;
        uintptr_t last = (ph[i].vaddr + ph[i].memsz - 1) / PAGE_SIZE;
        pages += (unsigned)(last - first + 1);
        for (unsigned j = 0; j < i; j++)
            if (ph[j].type == ELF_LOAD && ph[j].memsz &&
                first <= (ph[j].vaddr + ph[j].memsz - 1) / PAGE_SIZE &&
                ph[j].vaddr / PAGE_SIZE <= last)
                return false;
    }
    return pages && pages <= LIB_PAGES;
}

static void discard_frames(shared_library_t *library)
{
    for (unsigned i = 0; i < LIB_PAGES; i++) {
        if (library->frames[i])
            pmm_free_frame(library->frames[i]);
        library->frames[i] = NULL;
    }
}

static bool cache_readonly(const void *image, size_t size,
                           shared_library_t *library,
                           const elf_header_t **header_out)
{
    const elf_header_t *header;
    if (!valid_image(image, size, ELF_DYN, &header) ||
        !shared_segments_valid(image, size, header))
        return false;
    const program_header_t *ph = programs(image, header);
    for (unsigned i = 0; i < header->phnum; i++) {
        if (ph[i].type != ELF_LOAD || !ph[i].memsz ||
            (ph[i].flags & ELF_WRITE))
            continue;
        for (uint32_t page = ph[i].vaddr / PAGE_SIZE;
             page <= (ph[i].vaddr + ph[i].memsz - 1) / PAGE_SIZE; page++) {
            library->frames[page] = pmm_alloc_frame();
            if (!library->frames[page]) {
                discard_frames(library);
                return false;
            }
            memset(library->frames[page], 0, PAGE_SIZE);
        }
        uint32_t copied = 0;
        while (copied < ph[i].filesz) {
            uint32_t address = ph[i].vaddr + copied;
            uint32_t count = PAGE_SIZE - address % PAGE_SIZE;
            if (count > ph[i].filesz - copied)
                count = ph[i].filesz - copied;
            memcpy((uint8_t *)library->frames[address / PAGE_SIZE] +
                   address % PAGE_SIZE,
                   (const uint8_t *)image + ph[i].offset + copied, count);
            copied += count;
        }
    }
    library->ready = true;
    *header_out = header;
    return true;
}

static bool load_library(shared_library_t *library, user_space_t *space)
{
    char path[FS_PATH_MAX];
    strlcpy(path, "/apps/lib/", sizeof(path));
    strcat(path, library->name);
    fs_node_t *file;
    if (fs_resolve(fs_root(), path, &file) != FS_OK ||
        file->type != FS_FILE || !file->data_borrowed ||
        !file->data || file->size > 65536)
        return false;
    const elf_header_t *header;
    if (!library->ready) {
        if (!cache_readonly(file->data, file->size, library, &header))
            return false;
        klog("[ld  ] cached %s read-only pages\n", library->name);
    } else if (!valid_image(file->data, file->size, ELF_DYN, &header) ||
               !shared_segments_valid(file->data, file->size, header)) {
        return false;
    } else {
        klog("[ld  ] reused %s read-only pages\n", library->name);
    }
    const program_header_t *ph = programs(file->data, header);
    for (unsigned i = 0; i < header->phnum; i++) {
        if (ph[i].type != ELF_LOAD || !ph[i].memsz)
            continue;
        for (uint32_t page = ph[i].vaddr / PAGE_SIZE;
             page <= (ph[i].vaddr + ph[i].memsz - 1) / PAGE_SIZE; page++) {
            uintptr_t address = library->base + page * PAGE_SIZE;
            bool ok = (ph[i].flags & ELF_WRITE) ?
                      user_space_map(space, address) :
                      user_space_map_shared(space, address,
                                            library->frames[page]);
            if (!ok)
                return false;
        }
        if (ph[i].flags & ELF_WRITE)
            memcpy((void *)(library->base + ph[i].vaddr),
                   file->data + ph[i].offset, ph[i].filesz);
    }
    return true;
}

static bool valid_library_name(const char *name)
{
    size_t length = strlen(name);
    if (length < 7 || length >= FS_NAME_MAX ||
        strncmp(name, "lib", 3) ||
        strcmp(name + length - 3, ".so"))
        return false;
    for (size_t i = 0; i < length; i++) {
        char ch = name[i];
        if (!((ch >= 'a' && ch <= 'z') ||
              (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '-' ||
              ch == '_' || ch == '.'))
            return false;
        if (ch == '.' && name[i + 1] == '.')
            return false;
    }
    return true;
}

static int library_slot(const char *name)
{
    if (!valid_library_name(name))
        return -1;
    for (unsigned i = 0; i < LIB_LIMIT; i++)
        if (libraries[i].name[0] && !strcmp(libraries[i].name, name))
            return (int)i;
    char path[FS_PATH_MAX];
    strlcpy(path, "/apps/lib/", sizeof(path));
    strcat(path, name);
    fs_node_t *file;
    if (fs_resolve(fs_root(), path, &file) != FS_OK ||
        file->type != FS_FILE || !file->data_borrowed ||
        !file->data || file->size > 65536)
        return -1;
    for (unsigned i = 0; i < LIB_LIMIT; i++)
        if (!libraries[i].name[0]) {
            strlcpy(libraries[i].name, name, sizeof(libraries[i].name));
            libraries[i].base = USER_BASE + 0x100000u +
                                i * LIB_REGION_SIZE;
            return (int)i;
        }
    return -1;
}

static bool link_library(unsigned slot, user_space_t *space,
                         dynlink_state_t *state, uint32_t visiting);

static bool open_recursive(const char *name, user_space_t *space,
                           dynlink_state_t *state, uint32_t visiting,
                           int *slot_out)
{
    int slot = library_slot(name);
    if (slot < 0)
        return false;
    uint32_t bit = 1u << slot;
    if (visiting & bit)
        return false;
    if (!(state->mask & bit)) {
        state->mask |= bit;
        if (!load_library(&libraries[slot], space)) {
            if (!libraries[slot].ready)
                libraries[slot].name[0] = '\0';
            return false;
        }
        if (!link_library((unsigned)slot, space, state, visiting | bit))
            return false;
    }
    *slot_out = slot;
    return true;
}

int dynlink_open(const char *name, user_space_t *space, dynlink_state_t *state)
{
    if (!name || !space || !state || state->close_mask)
        return -1;
    dynlink_state_t transaction = *state;
    if (transaction.next_initializer == transaction.initializer_count)
        transaction.next_initializer = transaction.initializer_count = 0;
    uint32_t original = state->mask;
    int slot = -1;
    bool ok = open_recursive(name, space, &transaction, 0, &slot);
    if (!ok || transaction.opens[slot] == 255u) {
        klog("[ld  ] rejected %s\n", name);
        for (unsigned i = 0; i < LIB_LIMIT; i++) {
            if (!(transaction.mask & (1u << i)) || (original & (1u << i)))
                continue;
            for (unsigned p = 0; p < LIB_PAGES; p++)
                user_space_unmap(space, libraries[i].base + p * PAGE_SIZE);
        }
        return -1;
    }
    transaction.opens[slot]++;
    *state = transaction;
    return slot + 1;
}

static uint32_t reachable_after_close(const dynlink_state_t *state, int handle)
{
    uint32_t reachable = state->needed_roots;
    for (unsigned i = 0; i < LIB_LIMIT; i++)
        if (state->opens[i] > (handle == (int)i + 1 ? 1u : 0u))
            reachable |= 1u << i;
    for (;;) {
        uint32_t previous = reachable;
        for (unsigned i = 0; i < LIB_LIMIT; i++)
            if (reachable & (1u << i))
                reachable |= state->dependencies[i];
        if (previous == reachable)
            return reachable;
    }
}

int dynlink_prepare_close(int handle, dynlink_state_t *state)
{
    if (!state || state->close_mask || handle < 1 ||
        handle > (int)LIB_LIMIT || !state->opens[handle - 1])
        return -1;
    uint32_t removed = state->mask & ~reachable_after_close(state, handle);
    for (unsigned i = 0; i < state->finalizer_count; i++)
        if ((removed & (1u << state->finalizers[i].slot)) &&
            state->finalizers[i].address) {
            state->close_mask = removed;
            state->close_cursor = state->finalizer_count;
            state->close_handle = handle;
            return 1;
        }
    return 0;
}

uintptr_t dynlink_close_next_finalizer(dynlink_state_t *state)
{
    if (!state || !state->close_mask)
        return 0;
    while (state->close_cursor) {
        dynlink_finalizer_t *entry =
            &state->finalizers[--state->close_cursor];
        if (!(state->close_mask & (1u << entry->slot)) || !entry->address)
            continue;
        uintptr_t callback = entry->address;
        entry->address = 0;
        return callback;
    }
    return 0;
}

int dynlink_close(int handle, user_space_t *space, dynlink_state_t *state)
{
    if (!space || !state || handle < 1 || handle > (int)LIB_LIMIT ||
        !state->opens[handle - 1] ||
        (state->close_mask &&
         (state->close_handle != handle || state->close_cursor)))
        return -1;
    if (!state->close_mask) {
        uint32_t removed =
            state->mask & ~reachable_after_close(state, handle);
        for (unsigned i = 0; i < state->finalizer_count; i++)
            if ((removed & (1u << state->finalizers[i].slot)) &&
                state->finalizers[i].address)
                return -1;
    }
    state->close_mask = 0;
    state->close_handle = 0;
    state->opens[handle - 1]--;
    uint32_t reachable = reachable_after_close(state, 0);
    uint32_t removed = state->mask & ~reachable;
    for (unsigned i = 0; i < LIB_LIMIT; i++) {
        if (!(removed & (1u << i)))
            continue;
        for (unsigned p = 0; p < LIB_PAGES; p++)
            user_space_unmap(space, libraries[i].base + p * PAGE_SIZE);
        state->dependencies[i] = 0;
    }
    state->mask &= reachable;
    unsigned remaining = state->next_initializer;
    for (unsigned i = remaining; i < state->initializer_count; i++) {
        uintptr_t callback = state->initializers[i];
        bool unloaded = false;
        for (unsigned j = 0; j < LIB_LIMIT; j++)
            if ((removed & (1u << j)) &&
                callback >= libraries[j].base &&
                callback - libraries[j].base < LIB_REGION_SIZE)
                unloaded = true;
        if (!unloaded)
            state->initializers[remaining++] = callback;
    }
    state->initializer_count = remaining;
    remaining = 0;
    for (unsigned i = 0; i < state->finalizer_count; i++)
        if (!(removed & (1u << state->finalizers[i].slot)) &&
            state->finalizers[i].address)
            state->finalizers[remaining++] = state->finalizers[i];
    state->finalizer_count = remaining;
    return 0;
}

typedef struct {
    uint32_t strtab, strsz, symtab, syment, hash, gnu_hash, rel, relsz, relent;
    uint32_t jmprel, pltrelsz;
    uint32_t pltrel;
    uint32_t init, init_array, init_array_size;
    uint32_t fini, fini_array, fini_array_size;
    uint32_t needed[LIB_LIMIT];
    unsigned needed_count;
} dynamic_info_t;

static bool read_dynamic(uintptr_t address, size_t bytes, dynamic_info_t *info)
{
    if (!bytes || bytes > 1024 || bytes % sizeof(dynamic_entry_t))
        return false;
    memset(info, 0, sizeof(*info));
    const dynamic_entry_t *entries = (const dynamic_entry_t *)address;
    for (unsigned i = 0; i < bytes / sizeof(*entries); i++) {
        switch (entries[i].tag) {
        case DT_NULL:
            return true;
        case DT_NEEDED:
            if (info->needed_count == LIB_LIMIT)
                return false;
            info->needed[info->needed_count++] = entries[i].value;
            break;
        case DT_STRTAB: info->strtab = entries[i].value; break;
        case DT_STRSZ: info->strsz = entries[i].value; break;
        case DT_SYMTAB: info->symtab = entries[i].value; break;
        case DT_SYMENT: info->syment = entries[i].value; break;
        case DT_HASH: info->hash = entries[i].value; break;
        case DT_GNU_HASH: info->gnu_hash = entries[i].value; break;
        case DT_REL: info->rel = entries[i].value; break;
        case DT_RELSZ: info->relsz = entries[i].value; break;
        case DT_RELENT: info->relent = entries[i].value; break;
        case DT_JMPREL: info->jmprel = entries[i].value; break;
        case DT_PLTRELSZ: info->pltrelsz = entries[i].value; break;
        case DT_PLTREL: info->pltrel = entries[i].value; break;
        case DT_INIT: info->init = entries[i].value; break;
        case DT_INIT_ARRAY: info->init_array = entries[i].value; break;
        case DT_INIT_ARRAYSZ: info->init_array_size = entries[i].value; break;
        case DT_FINI: info->fini = entries[i].value; break;
        case DT_FINI_ARRAY: info->fini_array = entries[i].value; break;
        case DT_FINI_ARRAYSZ: info->fini_array_size = entries[i].value; break;
        case DT_RELA:
        case DT_TEXTREL:
        case DT_PREINIT_ARRAY:
        case DT_PREINIT_ARRAYSZ:
        case DT_VERSYM:
        case DT_VERDEF:
        case DT_VERDEFNUM:
        case DT_VERNEED:
        case DT_VERNEEDNUM:
            return false;
        default:
            break;
        }
    }
    return false;
}

static bool string_at(const char *strings, uint32_t size, uint32_t offset,
                      const char **value)
{
    if (offset >= size)
        return false;
    for (uint32_t i = offset; i < size; i++)
        if (!strings[i]) {
            *value = strings + offset;
            return true;
        }
    return false;
}

static bool dynamic_range(uintptr_t base, uint32_t offset, size_t length)
{
    if (base)
        return offset < LIB_REGION_SIZE &&
               length <= LIB_REGION_SIZE - offset;
    return offset >= USER_BASE && offset < USER_BASE + 0x100000u &&
           length <= USER_BASE + 0x100000u - offset;
}

static bool dynamic_symbol_count(const user_space_t *space, uintptr_t base,
                                 const dynamic_info_t *info, uint32_t *count)
{
    if (info->hash) {
        if (!dynamic_range(base, info->hash, 8) ||
            !mapped(space, base + info->hash, 8, false))
            return false;
        *count = ((const uint32_t *)(base + info->hash))[1];
        return *count > 0 && *count <= 128;
    }
    if (!info->gnu_hash ||
        !dynamic_range(base, info->gnu_hash, 16) ||
        !mapped(space, base + info->gnu_hash, 16, false))
        return false;
    const uint32_t *header = (const uint32_t *)(base + info->gnu_hash);
    uint32_t buckets = header[0], first = header[1], bloom = header[2];
    if (!buckets || buckets > 128 || !bloom || bloom > 32 ||
        !first || first > 128)
        return false;
    size_t table_size = 16 + (size_t)(bloom + buckets) * sizeof(uint32_t);
    if (!dynamic_range(base, info->gnu_hash, table_size) ||
        !mapped(space, base + info->gnu_hash, table_size, false))
        return false;
    const uint32_t *bucket = (const uint32_t *)(base + info->gnu_hash +
                                                16 + bloom * 4u);
    uint32_t maximum = first;
    for (uint32_t i = 0; i < buckets; i++) {
        if (!bucket[i])
            continue;
        if (bucket[i] < first || bucket[i] >= 128)
            return false;
        bool terminated = false;
        for (uint32_t n = bucket[i]; n < 128; n++) {
            size_t end = table_size + ((size_t)n - first + 1) * 4u;
            if (!dynamic_range(base, info->gnu_hash, end) ||
                !mapped(space, base + info->gnu_hash + end - 4, 4, false))
                return false;
            uint32_t chain = *(const uint32_t *)(base + info->gnu_hash +
                                                 end - 4);
            if (n + 1 > maximum)
                maximum = n + 1;
            if (chain & 1u) {
                terminated = true;
                break;
            }
        }
        if (!terminated)
            return false;
    }
    /* Undefined symbols are not in GNU hash buckets; a contiguous dynsym
     * followed by dynstr also bounds the executable's import table. */
    if (info->strtab > info->symtab &&
        !(info->gnu_hash > info->symtab &&
          info->gnu_hash < info->strtab)) {
        uint32_t bytes = info->strtab - info->symtab;
        if (bytes % sizeof(symbol_t) == 0 &&
            bytes / sizeof(symbol_t) >= maximum &&
            bytes / sizeof(symbol_t) <= 128)
            maximum = bytes / sizeof(symbol_t);
    }
    *count = maximum;
    return true;
}

static bool export_symbol(const shared_library_t *library,
                          const user_space_t *space, const char *name,
                          uintptr_t *address)
{
    fs_node_t *file;
    char path[FS_PATH_MAX];
    strlcpy(path, "/apps/lib/", sizeof(path));
    strcat(path, library->name);
    if (fs_resolve(fs_root(), path, &file) != FS_OK)
        return false;
    const elf_header_t *header;
    if (!valid_image(file->data, file->size, ELF_DYN, &header))
        return false;
    const program_header_t *ph = programs(file->data, header);
    for (unsigned i = 0; i < header->phnum; i++) {
        if (ph[i].type != ELF_DYNAMIC || ph[i].filesz > ph[i].memsz ||
            ph[i].vaddr >= LIB_REGION_SIZE ||
            ph[i].filesz > LIB_REGION_SIZE - ph[i].vaddr)
            continue;
        uintptr_t dyn = library->base + ph[i].vaddr;
        if (!mapped(space, dyn, ph[i].filesz, false))
            return false;
        dynamic_info_t info;
        if (!read_dynamic(dyn, ph[i].filesz, &info) ||
            (!info.hash && !info.gnu_hash) || !info.strtab || !info.symtab ||
            !info.strsz || info.strsz > 4096 ||
            info.syment != sizeof(symbol_t) ||
            info.strtab > LIB_REGION_SIZE - info.strsz ||
            info.symtab >= LIB_REGION_SIZE)
            return false;
        uint32_t symbols = 0;
        if (!dynamic_symbol_count(space, library->base, &info, &symbols) ||
            !mapped(space, library->base + info.strtab, info.strsz, false) ||
            !mapped(space, library->base + info.symtab,
                    symbols * sizeof(symbol_t), false))
            return false;
        const char *strings = (const char *)(library->base + info.strtab);
        const symbol_t *table = (const symbol_t *)(library->base + info.symtab);
        for (uint32_t n = 1; n < symbols; n++) {
            const char *candidate;
            if (table[n].section && (table[n].info >> 4) == 1 &&
                ((table[n].info & 15) == 2 ||
                 (table[n].info & 15) == 1) &&
                string_at(strings, info.strsz, table[n].name, &candidate) &&
                !strcmp(candidate, name) &&
                table[n].value < LIB_REGION_SIZE &&
                mapped(space, library->base + table[n].value, 1, false) &&
                ((table[n].info & 15) != 2 ||
                 !((space->table[(library->base + table[n].value - USER_BASE) /
                                  PAGE_SIZE]) & 2u))) {
                *address = library->base + table[n].value;
                return true;
            }
        }
        return false;
    }
    return false;
}

static bool write_target(const program_header_t *ph, unsigned count,
                         const user_space_t *space, uintptr_t base,
                         uintptr_t address)
{
    if (address < base)
        return false;
    uintptr_t offset = address - base;
    for (unsigned i = 0; i < count; i++)
        if (ph[i].type == ELF_LOAD && (ph[i].flags & ELF_WRITE) &&
            offset >= ph[i].vaddr &&
            ph[i].memsz >= sizeof(uint32_t) &&
            offset - ph[i].vaddr <= ph[i].memsz - sizeof(uint32_t) &&
            mapped(space, address, sizeof(uint32_t), true))
            return true;
    return false;
}

static bool library_range(uint32_t offset, size_t length)
{
    return offset < LIB_REGION_SIZE && length <= LIB_REGION_SIZE - offset;
}

static bool link_relocations(const program_header_t *ph, unsigned phnum,
                             const user_space_t *space, uintptr_t base,
                             const dynamic_info_t *info, uint32_t mask)
{
    if (info->relsz > 128 * sizeof(relocation_t) ||
        info->pltrelsz > 128 * sizeof(relocation_t) ||
        info->relsz % sizeof(relocation_t) ||
        info->pltrelsz % sizeof(relocation_t) ||
        (info->relsz && (!info->rel ||
                         info->relent != sizeof(relocation_t))) ||
        (info->pltrelsz && (!info->jmprel || info->pltrel != DT_REL)))
        return false;
    if (!library_range(info->strtab, info->strsz) ||
        !library_range(info->symtab, sizeof(symbol_t)) ||
        (info->relsz && !library_range(info->rel, info->relsz)) ||
        (info->pltrelsz && !library_range(info->jmprel, info->pltrelsz)) ||
        !mapped(space, base + info->strtab, info->strsz, false))
        return false;
    uint32_t symbols = 0;
    if (!dynamic_symbol_count(space, base, info, &symbols) ||
        !library_range(info->symtab, symbols * sizeof(symbol_t)) ||
        !mapped(space, base + info->symtab, symbols * sizeof(symbol_t),
                false))
        return false;
    const char *strings = (const char *)(base + info->strtab);
    const symbol_t *table = (const symbol_t *)(base + info->symtab);
    const uint32_t offsets[] = { info->rel, info->jmprel };
    const uint32_t sizes[] = { info->relsz, info->pltrelsz };
    for (unsigned group = 0; group < 2; group++) {
        if (!sizes[group])
            continue;
        if (!mapped(space, base + offsets[group], sizes[group], false))
            return false;
        const relocation_t *rel = (const relocation_t *)(base + offsets[group]);
        for (unsigned n = 0; n < sizes[group] / sizeof(*rel); n++) {
            uint32_t index = rel[n].info >> 8;
            uint32_t type = rel[n].info & 255u;
            if (!library_range(rel[n].offset, sizeof(uint32_t)) ||
                !write_target(ph, phnum, space, base,
                              base + rel[n].offset))
                return false;
            uint32_t *target = (uint32_t *)(base + rel[n].offset);
            if (type == R_386_RELATIVE) {
                if (index || !library_range(*target, 1) ||
                    !mapped(space, base + *target, 1, false))
                    return false;
                *target += (uint32_t)base;
                continue;
            }
            if ((type != R_386_GLOB_DAT && type != R_386_JUMP_SLOT &&
                 type != R_386_32) || !index || index >= symbols)
                return false;
            uintptr_t symbol = 0;
            if (table[index].section) {
                if (!library_range(table[index].value, 1) ||
                    !mapped(space, base + table[index].value, 1, false))
                    return false;
                symbol = base + table[index].value;
            } else {
                const char *name;
                if (!string_at(strings, info->strsz, table[index].name, &name))
                    return false;
                for (unsigned j = 0; j < LIB_LIMIT; j++)
                    if ((mask & (1u << j)) &&
                        export_symbol(&libraries[j], space, name, &symbol))
                        break;
            }
            if (!symbol)
                return false;
            uint32_t value = (uint32_t)symbol;
            if (type == R_386_32) {
                if (value > UINT32_MAX - *target ||
                    !mapped(space, value + *target, 1, false))
                    return false;
                value += *target;
            }
            *target = value;
        }
    }
    return true;
}

static bool executable_library_address(const program_header_t *ph,
                                       unsigned count,
                                       const user_space_t *space,
                                       uintptr_t base, uintptr_t address)
{
    if (address < base || !library_range(address - base, 1) ||
        !mapped(space, address, 1, false) ||
        (space->table[(address - USER_BASE) / PAGE_SIZE] & 2u))
        return false;
    for (unsigned i = 0; i < count; i++)
        if (ph[i].type == ELF_LOAD && (ph[i].flags & ELF_EXECUTE) &&
            address - base >= ph[i].vaddr &&
            address - base - ph[i].vaddr < ph[i].memsz)
            return true;
    return false;
}

static bool append_initializers(const program_header_t *ph, unsigned count,
                                const user_space_t *space, uintptr_t base,
                                const dynamic_info_t *info,
                                dynlink_state_t *state)
{
    if (info->init_array_size % sizeof(uint32_t) ||
        info->init_array_size > DYNLINK_INIT_LIMIT * sizeof(uint32_t) ||
        (info->init_array_size && !info->init_array) ||
        (info->init_array && !info->init_array_size))
        return false;
    if (info->init) {
        uintptr_t address = base + info->init;
        if (!executable_library_address(ph, count, space, base, address) ||
            state->initializer_count == DYNLINK_INIT_LIMIT)
            return false;
        state->initializers[state->initializer_count++] = address;
    }
    if (!info->init_array_size)
        return true;
    if (!library_range(info->init_array, info->init_array_size) ||
        !mapped(space, base + info->init_array, info->init_array_size, false))
        return false;
    const uint32_t *array = (const uint32_t *)(base + info->init_array);
    for (unsigned i = 0; i < info->init_array_size / sizeof(*array); i++) {
        uintptr_t address = array[i];
        if (address == 0 || address == UINT32_MAX)
            continue;
        if (!executable_library_address(ph, count, space, base, address) ||
            state->initializer_count == DYNLINK_INIT_LIMIT)
            return false;
        state->initializers[state->initializer_count++] = address;
    }
    return true;
}

static bool append_finalizers(const program_header_t *ph, unsigned count,
                              const user_space_t *space, uintptr_t base,
                              const dynamic_info_t *info, unsigned slot,
                              dynlink_state_t *state)
{
    if (info->fini_array_size % sizeof(uint32_t) ||
        info->fini_array_size > DYNLINK_FINI_LIMIT * sizeof(uint32_t) ||
        (info->fini_array_size && !info->fini_array) ||
        (info->fini_array && !info->fini_array_size))
        return false;
    if (info->fini) {
        uintptr_t address = base + info->fini;
        if (!executable_library_address(ph, count, space, base, address) ||
            state->finalizer_count == DYNLINK_FINI_LIMIT)
            return false;
        state->finalizers[state->finalizer_count++] =
            (dynlink_finalizer_t){ address, (uint8_t)slot };
    }
    if (!info->fini_array_size)
        return true;
    if (!library_range(info->fini_array, info->fini_array_size) ||
        !mapped(space, base + info->fini_array, info->fini_array_size, false))
        return false;
    const uint32_t *array = (const uint32_t *)(base + info->fini_array);
    for (unsigned i = 0; i < info->fini_array_size / sizeof(*array); i++) {
        uintptr_t address = array[i];
        if (!address || address == UINT32_MAX)
            continue;
        if (!executable_library_address(ph, count, space, base, address) ||
            state->finalizer_count == DYNLINK_FINI_LIMIT)
            return false;
        state->finalizers[state->finalizer_count++] =
            (dynlink_finalizer_t){ address, (uint8_t)slot };
    }
    return true;
}

static bool link_library(unsigned slot, user_space_t *space,
                         dynlink_state_t *state, uint32_t visiting)
{
    shared_library_t *library = &libraries[slot];
    char path[FS_PATH_MAX];
    strlcpy(path, "/apps/lib/", sizeof(path));
    strcat(path, library->name);
    fs_node_t *file;
    const elf_header_t *header;
    if (fs_resolve(fs_root(), path, &file) != FS_OK ||
        !valid_image(file->data, file->size, ELF_DYN, &header))
        return false;
    const program_header_t *ph = programs(file->data, header);
    const program_header_t *dynamic = NULL;
    for (unsigned i = 0; i < header->phnum; i++)
        if (ph[i].type == ELF_DYNAMIC) {
            if (dynamic)
                return false;
            dynamic = &ph[i];
        }
    if (!dynamic || !library_range(dynamic->vaddr, dynamic->filesz) ||
        !mapped(space, library->base + dynamic->vaddr,
                dynamic->filesz, false))
        return false;
    dynamic_info_t info;
    if (!read_dynamic(library->base + dynamic->vaddr,
                      dynamic->filesz, &info) ||
        (!info.hash && !info.gnu_hash) || !info.strtab || !info.symtab ||
        !info.strsz || info.strsz > 4096 ||
        info.syment != sizeof(symbol_t) ||
        !library_range(info.strtab, info.strsz) ||
        !mapped(space, library->base + info.strtab, info.strsz, false))
        return false;
    const char *strings = (const char *)(library->base + info.strtab);
    uint32_t dependencies = 0;
    for (unsigned i = 0; i < info.needed_count; i++) {
        const char *name;
        int dependency;
        if (!string_at(strings, info.strsz, info.needed[i], &name) ||
            !open_recursive(name, space, state, visiting, &dependency))
            return false;
        dependencies |= 1u << dependency;
    }
    if (!link_relocations(ph, header->phnum, space, library->base,
                          &info, state->mask) ||
        !append_initializers(ph, header->phnum, space, library->base,
                             &info, state) ||
        !append_finalizers(ph, header->phnum, space, library->base,
                           &info, slot, state))
        return false;
    state->dependencies[slot] = dependencies;
    return true;
}

bool dynlink_load(const void *image, size_t size, user_space_t *space,
                  dynlink_state_t *state)
{
    const elf_header_t *header;
    if (!valid_image(image, size, ELF_EXEC, &header))
        return false;
    const program_header_t *ph = programs(image, header);
    const program_header_t *dynamic = NULL;
    for (unsigned i = 0; i < header->phnum; i++)
        if (ph[i].type == ELF_INTERP || ph[i].type == ELF_TLS)
            return false;
    for (unsigned i = 0; i < header->phnum; i++)
        if (ph[i].type == ELF_DYNAMIC && ph[i].filesz) {
            if (dynamic)
                return false;
            dynamic = &ph[i];
        }
    if (!dynamic)
        return true;
    if (dynamic->filesz > dynamic->memsz ||
        !mapped(space, dynamic->vaddr, dynamic->filesz, false))
        return false;
    dynamic_info_t info;
    if (!read_dynamic(dynamic->vaddr, dynamic->filesz, &info) ||
        !info.needed_count || info.strsz > 4096 || !info.strsz ||
        !info.strtab || !info.symtab || (!info.hash && !info.gnu_hash) ||
        info.syment != sizeof(symbol_t) ||
        !mapped(space, info.strtab, info.strsz, false) ||
        !dynamic_range(0, info.symtab, sizeof(symbol_t)))
        return false;
    uint32_t symbol_count = 0;
    if (!dynamic_symbol_count(space, 0, &info, &symbol_count) ||
        !mapped(space, info.symtab, symbol_count * sizeof(symbol_t), false))
        return false;
    const char *strings = (const char *)(uintptr_t)info.strtab;
    const symbol_t *symbols = (const symbol_t *)(uintptr_t)info.symtab;
    uint32_t loaded = 0;
    for (unsigned i = 0; i < info.needed_count; i++) {
        const char *name;
        if (!string_at(strings, info.strsz, info.needed[i], &name))
            return false;
        int handle = dynlink_open(name, space, state);
        if (handle < 1 || (loaded & (1u << (handle - 1))))
            return false;
        state->needed_roots |= 1u << (handle - 1);
        state->opens[handle - 1]--;
        loaded |= 1u << (handle - 1);
    }
    if (info.init || info.init_array || info.init_array_size ||
        info.fini || info.fini_array || info.fini_array_size ||
        info.rel || info.relsz ||
        (info.pltrelsz && (info.pltrel != DT_REL || !info.jmprel)) ||
        info.pltrelsz > 128 * sizeof(relocation_t) ||
        info.pltrelsz % sizeof(relocation_t) ||
        (info.pltrelsz &&
         !mapped(space, info.jmprel, info.pltrelsz, false)))
        return false;
    const relocation_t *relocations = (const relocation_t *)(uintptr_t)info.jmprel;
    for (unsigned i = 0; i < info.pltrelsz / sizeof(*relocations); i++) {
        uint32_t index = relocations[i].info >> 8;
        uint32_t type = relocations[i].info & 255u;
        const char *name;
        if ((type != R_386_JUMP_SLOT && type != R_386_GLOB_DAT) ||
            !index || index >= symbol_count ||
            !string_at(strings, info.strsz, symbols[index].name, &name) ||
            !write_target(ph, header->phnum, space, 0,
                          relocations[i].offset))
            return false;
        uintptr_t target = 0;
        for (unsigned j = 0; j < ARRAY_SIZE(libraries); j++)
            if ((loaded & (1u << j)) &&
                export_symbol(&libraries[j], space, name, &target))
                break;
        if (!target)
            return false;
        *(uint32_t *)(uintptr_t)relocations[i].offset = (uint32_t)target;
    }
    return true;
}

uintptr_t dynlink_next_initializer(dynlink_state_t *state)
{
    if (!state || state->next_initializer >= state->initializer_count)
        return 0;
    return state->initializers[state->next_initializer++];
}

uintptr_t dynlink_next_finalizer(dynlink_state_t *state)
{
    if (!state)
        return 0;
    while (state->finalizer_count) {
        dynlink_finalizer_t entry =
            state->finalizers[--state->finalizer_count];
        if (entry.address && (state->mask & (1u << entry.slot)))
            return entry.address;
    }
    return 0;
}

uintptr_t dynlink_symbol(int handle, const char *name,
                         const user_space_t *space,
                         const dynlink_state_t *state)
{
    if (!name || !space || !state ||
        handle < 1 || handle > (int)LIB_LIMIT ||
        !(state->mask & (1u << (handle - 1))) ||
        !state->opens[handle - 1])
        return 0;
    uintptr_t address = 0;
    if (!export_symbol(&libraries[handle - 1], space, name, &address))
        return 0;
    return address;
}
