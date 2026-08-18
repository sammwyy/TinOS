/* The subset of the Multiboot2 boot information structures TinOS reads. */
#ifndef TINOS_MULTIBOOT2_H
#define TINOS_MULTIBOOT2_H

#include <tinos/kernel.h>
#include <tinos/types.h>

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289u

enum {
    MB_TAG_END          = 0,
    MB_TAG_CMDLINE      = 1,
    MB_TAG_BOOT_LOADER  = 2,
    MB_TAG_MODULE       = 3,
    MB_TAG_BASIC_MEMINFO = 4,
    MB_TAG_BOOTDEV      = 5,
    MB_TAG_MMAP         = 6,
    MB_TAG_FRAMEBUFFER  = 8,
    MB_TAG_ACPI_OLD     = 14, /* ACPI 1.0 RSDP (20-byte, RSDT only) */
    MB_TAG_ACPI_NEW     = 15, /* ACPI >=2.0 RSDP (adds the XSDT pointer) */
};

typedef struct PACKED {
    uint32_t total_size;
    uint32_t reserved;
} mb_info_t;

typedef struct PACKED {
    uint32_t type;
    uint32_t size;
} mb_tag_t;

typedef struct PACKED {
    uint32_t type;
    uint32_t size;
    char     string[0];
} mb_tag_string_t;

typedef struct PACKED {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower; /* KiB below 1 MiB */
    uint32_t mem_upper; /* KiB above 1 MiB */
} mb_tag_meminfo_t;

/* A GRUB "module2" load: an arbitrary blob placed in RAM alongside the
 * kernel before boot. Used to hand the ramfs.tar archive over without a
 * filesystem driver for the boot media itself. */
typedef struct PACKED {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char     cmdline[0];
} mb_tag_module_t;

#define MB_MEMORY_AVAILABLE 1
#define MB_MEMORY_ACPI_RECLAIMABLE 3

typedef struct PACKED {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} mb_mmap_entry_t;

typedef struct PACKED {
    uint32_t        type;
    uint32_t        size;
    uint32_t        entry_size;
    uint32_t        entry_version;
    mb_mmap_entry_t entries[0];
} mb_tag_mmap_t;

typedef struct PACKED {
    uint32_t type;
    uint32_t size;
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  fb_type; /* 0 indexed, 1 RGB, 2 EGA text */
    uint8_t  reserved;
} mb_tag_framebuffer_t;

/* The tag payload is the raw ACPI RSDP structure copied verbatim by GRUB;
 * its own fields (not this wrapper) tell us whether it is the 20-byte
 * ACPI 1.0 shape or the extended >=2.0 shape with an XSDT pointer. */
typedef struct PACKED {
    uint32_t type;
    uint32_t size;
    uint8_t  rsdp[0];
} mb_tag_acpi_t;

/* Iterate over the tags of a multiboot2 information block. */
static inline mb_tag_t *mb_first_tag(const mb_info_t *info)
{
    return (mb_tag_t *)((uintptr_t)info + 8);
}

static inline mb_tag_t *mb_next_tag(const mb_tag_t *tag)
{
    return (mb_tag_t *)ALIGN_UP((uintptr_t)tag + tag->size, 8);
}

#define MB_FOR_EACH_TAG(info, tag)                                        \
    for (mb_tag_t *tag = mb_first_tag(info); tag->type != MB_TAG_END;     \
         tag = mb_next_tag(tag))

#endif /* TINOS_MULTIBOOT2_H */
