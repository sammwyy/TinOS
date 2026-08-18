/* Minimal ACPI reader: just enough to find the MADT (APIC table) so SMP
 * bring-up knows the local APIC base address and which CPU (APIC) IDs
 * exist. Everything here is read-only; TinOS does not implement AML/the
 * ACPI runtime (power management, device enumeration via ACPI, ...). */
#ifndef TINOS_ACPI_H
#define TINOS_ACPI_H

#include <tinos/multiboot2.h>
#include <tinos/types.h>

#define ACPI_MAX_CPUS 32

typedef struct {
    uint32_t lapic_address;
    uint8_t  apic_ids[ACPI_MAX_CPUS];
    size_t   cpu_count;
} acpi_madt_info_t;

/* Finds the RSDP GRUB copied into the multiboot2 info block, walks
 * RSDT/XSDT to the MADT, and fills `out`. Returns false if no usable,
 * checksum-valid MADT was found - the caller should then treat the
 * machine as single-CPU rather than guess. */
bool acpi_find_madt(const mb_info_t *mbi, acpi_madt_info_t *out);

#endif /* TINOS_ACPI_H */
