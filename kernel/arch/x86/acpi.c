/* RSDP -> RSDT/XSDT -> MADT walk. Paging is identity-mapped, so every
 * physical address ACPI hands us is already a valid pointer. */

#include <tinos/acpi.h>
#include <tinos/kernel.h>
#include <tinos/string.h>

typedef struct PACKED {
    char     sig[8]; /* "RSD PTR " */
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;
} acpi_rsdp_v1_t;

typedef struct PACKED {
    acpi_rsdp_v1_t v1;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} acpi_rsdp_v2_t;

typedef struct PACKED {
    char     sig[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_sdt_header_t;

typedef struct PACKED {
    acpi_sdt_header_t header;
    uint32_t local_apic_address;
    uint32_t flags;
    uint8_t  entries[0];
} acpi_madt_t;

typedef struct PACKED {
    uint8_t type;
    uint8_t length;
} acpi_madt_entry_t;

#define ACPI_MADT_TYPE_LOCAL_APIC 0
#define ACPI_MADT_LAPIC_ENABLED   (1u << 0)

typedef struct PACKED {
    acpi_madt_entry_t header;
    uint8_t  acpi_processor_id;
    uint8_t  apic_id;
    uint32_t flags;
} acpi_madt_lapic_t;

static bool checksum_ok(const void *data, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++)
        sum = (uint8_t)(sum + ((const uint8_t *)data)[i]);
    return sum == 0;
}

static const acpi_rsdp_v2_t *find_rsdp(const boot_info_t *info)
{
    if (!info->rsdp_address)
        return NULL;
    const acpi_rsdp_v2_t *rsdp =
        (const acpi_rsdp_v2_t *)(uintptr_t)info->rsdp_address;
    if (memcmp(rsdp->v1.sig, "RSD PTR ", 8) != 0 ||
        !checksum_ok(&rsdp->v1, sizeof(rsdp->v1)))
        return NULL;
    if (rsdp->v1.revision >= 2 &&
        (!rsdp->length || !checksum_ok(rsdp, rsdp->length)))
        return NULL;
    return rsdp;
}

/* Scans an RSDT (32-bit table pointers) or XSDT (64-bit, truncated - every
 * real address QEMU/real firmware hands out fits in 32 bits here) for a
 * table whose signature matches, validating its checksum before trusting
 * it. */
static const acpi_sdt_header_t *find_table(const acpi_sdt_header_t *root,
                                           bool wide, const char *sig)
{
    if (!checksum_ok(root, root->length))
        return NULL;
    size_t stride = wide ? 8 : 4;
    size_t count  = (root->length - sizeof(*root)) / stride;
    const uint8_t *entries = (const uint8_t *)(root + 1);

    for (size_t i = 0; i < count; i++) {
        uintptr_t addr;
        if (wide) {
            uint64_t addr64;
            memcpy(&addr64, entries + i * 8, 8);
            addr = (uintptr_t)addr64;
        } else {
            uint32_t addr32;
            memcpy(&addr32, entries + i * 4, 4);
            addr = (uintptr_t)addr32;
        }
        const acpi_sdt_header_t *table = (const acpi_sdt_header_t *)addr;
        if (memcmp(table->sig, sig, 4) == 0 &&
            checksum_ok(table, table->length))
            return table;
    }
    return NULL;
}

bool acpi_find_madt(const boot_info_t *info, acpi_madt_info_t *out)
{
    memset(out, 0, sizeof(*out));

    const acpi_rsdp_v2_t *rsdp = find_rsdp(info);
    if (!rsdp)
        return false;

    const acpi_sdt_header_t *root;
    bool wide;
    if (rsdp->v1.revision >= 2 && rsdp->xsdt_address) {
        root = (const acpi_sdt_header_t *)(uintptr_t)rsdp->xsdt_address;
        wide = true;
    } else {
        root = (const acpi_sdt_header_t *)(uintptr_t)rsdp->v1.rsdt_address;
        wide = false;
    }
    if (memcmp(root->sig, wide ? "XSDT" : "RSDT", 4) != 0)
        return false;

    const acpi_madt_t *madt =
        (const acpi_madt_t *)find_table(root, wide, "APIC");
    if (!madt)
        return false;

    out->lapic_address = madt->local_apic_address;

    const uint8_t *p   = madt->entries;
    const uint8_t *end = (const uint8_t *)madt + madt->header.length;
    while (p + sizeof(acpi_madt_entry_t) <= end) {
        const acpi_madt_entry_t *entry = (const acpi_madt_entry_t *)p;
        if (entry->length == 0 || p + entry->length > end)
            break; /* malformed table - stop rather than run off the end */
        if (entry->type == ACPI_MADT_TYPE_LOCAL_APIC &&
            entry->length >= sizeof(acpi_madt_lapic_t)) {
            const acpi_madt_lapic_t *lapic = (const acpi_madt_lapic_t *)p;
            if ((lapic->flags & ACPI_MADT_LAPIC_ENABLED) &&
                out->cpu_count < ACPI_MAX_CPUS)
                out->apic_ids[out->cpu_count++] = lapic->apic_id;
        }
        p += entry->length;
    }
    return out->cpu_count > 0;
}
