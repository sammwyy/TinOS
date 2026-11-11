/* Boot contract shared by the kernel and platform-specific loaders. */
#ifndef TINOS_BOOT_INFO_H
#define TINOS_BOOT_INFO_H

#include <tinos/kernel.h>
#include <tinos/types.h>

#define TINOS_BOOT_MAGIC 0x54494e4fu
#define TINOS_BOOT_VERSION 1u
#define BOOT_MEMORY_USABLE 1u
#define BOOT_MEMORY_ENTRIES 128u

typedef struct PACKED {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} boot_memory_entry_t;

typedef struct PACKED {
    uint32_t size;
    uint32_t version;
    uint32_t memory_count;
    uint32_t initrd_start;
    uint32_t initrd_end;
    uint32_t rsdp_address;
    char command_line[128];
    boot_memory_entry_t memory[BOOT_MEMORY_ENTRIES];
} boot_info_t;

_Static_assert(sizeof(boot_memory_entry_t) == 24, "boot memory entry size");
_Static_assert(__builtin_offsetof(boot_info_t, memory) == 152,
               "boot memory map offset");
_Static_assert(sizeof(boot_info_t) == 152 + 24 * BOOT_MEMORY_ENTRIES,
               "boot information size");

#endif /* TINOS_BOOT_INFO_H */
