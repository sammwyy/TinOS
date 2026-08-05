/*
 * Small, read-only FAT16 volume importer.
 *
 * FAT is intentionally kept above the block layer. This first filesystem
 * implementation imports a volume into TinOS's RAM tree; write-through VFS
 * mounts can be added later without changing ATA, USB, or VirtIO drivers.
 */

#include <tinos/fat16.h>
#include <tinos/heap.h>
#include <tinos/kernel.h>
#include <tinos/string.h>

#define FAT16_MAX_DEPTH 16
#define FAT16_EOC        0xFFF8u
#define FAT16_BAD        0xFFF7u
#define FAT_READ_BATCH   8u

typedef struct {
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entries;
    uint32_t total_sectors;
    uint16_t sectors_per_fat;
    uint32_t fat_lba;
    uint32_t root_lba;
    uint32_t root_sectors;
    uint32_t data_lba;
    uint32_t cluster_count;
    uint32_t fat_cache_lba;
    bool     fat_cache_valid;
    uint8_t  fat_cache[BLOCK_SECTOR_SIZE];
} fat16_volume_t;

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)le16(p) | ((uint32_t)le16(p + 2) << 16);
}

static bool fat16_sector(block_device_t *device, uint32_t lba, uint8_t *buffer)
{
    return block_read(device, lba, buffer, 1);
}

static bool fat16_next(block_device_t *device, fat16_volume_t *volume,
                       uint16_t cluster, uint16_t *next)
{
    uint32_t byte_offset = (uint32_t)cluster * 2;
    uint32_t lba = volume->fat_lba + byte_offset / BLOCK_SECTOR_SIZE;
    if (!volume->fat_cache_valid || volume->fat_cache_lba != lba) {
        if (!fat16_sector(device, lba, volume->fat_cache)) return false;
        volume->fat_cache_lba = lba;
        volume->fat_cache_valid = true;
    }
    *next = le16(volume->fat_cache + byte_offset % BLOCK_SECTOR_SIZE);
    return true;
}

static bool fat16_cluster_valid(const fat16_volume_t *volume, uint16_t cluster)
{
    return cluster >= 2 && cluster < volume->cluster_count + 2;
}

static uint32_t fat16_cluster_lba(const fat16_volume_t *volume,
                                  uint16_t cluster)
{
    return volume->data_lba + (uint32_t)(cluster - 2) *
                                  volume->sectors_per_cluster;
}

static void fat16_name(const uint8_t *entry, char *name, size_t capacity)
{
    size_t used = 0;
    for (size_t i = 0; i < 8 && entry[i] != ' '; i++) {
        if (used + 1 < capacity)
            name[used++] = (char)entry[i];
    }
    size_t extension = 0;
    for (size_t i = 0; i < 3 && entry[8 + i] != ' '; i++)
        extension++;
    if (extension && used + 1 < capacity)
        name[used++] = '.';
    for (size_t i = 0; i < extension && used + 1 < capacity; i++)
        name[used++] = (char)entry[8 + i];
    name[used] = '\0';
}

static bool fat16_import_file(block_device_t *device,
                              fat16_volume_t *volume,
                              fs_node_t *parent, const uint8_t *entry,
                              const char *name)
{
    fs_node_t *file = NULL;
    if (fs_create(parent, name, &file) != FS_OK)
        return false;

    uint32_t remaining = le32(entry + 28);
    uint16_t cluster = le16(entry + 26);
    uint8_t sectors[BLOCK_SECTOR_SIZE * FAT_READ_BATCH];
    uint32_t guard = 0;
    while (remaining) {
        if (!fat16_cluster_valid(volume, cluster) ||
            cluster == FAT16_BAD || guard++ > volume->cluster_count)
            return false;
        uint32_t lba = fat16_cluster_lba(volume, cluster);
        for (uint32_t s = 0; s < volume->sectors_per_cluster && remaining;) {
            uint32_t batch = MIN((uint32_t)FAT_READ_BATCH,
                                 volume->sectors_per_cluster - s);
            batch = MIN(batch, (remaining + BLOCK_SECTOR_SIZE - 1) /
                                BLOCK_SECTOR_SIZE);
            if (!block_read(device, lba + s, sectors, batch))
                return false;
            for (uint32_t b = 0; b < batch && remaining; b++) {
                uint32_t count = MIN(remaining, BLOCK_SECTOR_SIZE);
                if (fs_append(file, sectors + b * BLOCK_SECTOR_SIZE, count) != FS_OK)
                    return false;
                remaining -= count;
            }
            s += batch;
        }
        if (!remaining)
            break;
        if (!fat16_next(device, volume, cluster, &cluster))
            return false;
        if (cluster >= FAT16_EOC)
            return false;
    }
    return true;
}

static bool fat16_import_directory(block_device_t *device,
                                   fat16_volume_t *volume,
                                   fs_node_t *parent, uint16_t first_cluster,
                                   bool root, unsigned depth);

static bool fat16_import_entry(block_device_t *device,
                               fat16_volume_t *volume, fs_node_t *parent,
                               const uint8_t *entry, unsigned depth)
{
    if (entry[0] == 0x00 || entry[0] == 0xE5 || entry[11] == 0x0F ||
        (entry[11] & 0x08))
        return true;

    char name[FS_NAME_MAX];
    fat16_name(entry, name, sizeof(name));
    if (!name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return true;

    bool directory = (entry[11] & 0x10) != 0;
    if (directory) {
        fs_node_t *child = NULL;
        if (fs_mkdir(parent, name, &child) != FS_OK)
            return false;
        if (depth >= FAT16_MAX_DEPTH)
            return false;
        return fat16_import_directory(device, volume, child,
                                       le16(entry + 26), false, depth + 1);
    }
    return fat16_import_file(device, volume, parent, entry, name);
}

static bool fat16_import_directory(block_device_t *device,
                                   fat16_volume_t *volume,
                                   fs_node_t *parent, uint16_t first_cluster,
                                   bool root, unsigned depth)
{
    uint8_t sector[BLOCK_SECTOR_SIZE];
    if (root) {
        for (uint32_t s = 0; s < volume->root_sectors; s++) {
            if (!fat16_sector(device, volume->root_lba + s, sector))
                return false;
            for (size_t offset = 0; offset < BLOCK_SECTOR_SIZE; offset += 32) {
                if (sector[offset] == 0x00)
                    return true;
                if (!fat16_import_entry(device, volume, parent,
                                        sector + offset, depth))
                    return false;
            }
        }
        return true;
    }

    uint16_t cluster = first_cluster;
    uint32_t guard = 0;
    while (fat16_cluster_valid(volume, cluster) &&
           cluster < FAT16_EOC && guard++ <= volume->cluster_count) {
        uint32_t lba = fat16_cluster_lba(volume, cluster);
        for (uint32_t s = 0; s < volume->sectors_per_cluster; s++) {
            if (!fat16_sector(device, lba + s, sector))
                return false;
            for (size_t offset = 0; offset < BLOCK_SECTOR_SIZE; offset += 32) {
                if (sector[offset] == 0x00)
                    return true;
                if (!fat16_import_entry(device, volume, parent,
                                        sector + offset, depth))
                    return false;
            }
        }
        if (!fat16_next(device, volume, cluster, &cluster))
            return false;
    }
    return cluster >= FAT16_EOC;
}

bool fat16_import(block_device_t *device, fs_node_t *mountpoint)
{
    if (!device || !mountpoint || mountpoint->type != FS_DIR)
        return false;

    uint8_t boot[BLOCK_SECTOR_SIZE];
    if (!fat16_sector(device, 0, boot) || le16(boot + 510) != 0xAA55)
        return false;

    fat16_volume_t volume;
    volume.bytes_per_sector      = le16(boot + 11);
    volume.sectors_per_cluster   = boot[13];
    volume.reserved_sectors      = le16(boot + 14);
    volume.fat_count             = boot[16];
    volume.root_entries          = le16(boot + 17);
    volume.sectors_per_fat       = le16(boot + 22);
    volume.total_sectors        = le16(boot + 19);
    if (!volume.total_sectors)
        volume.total_sectors = le32(boot + 32);

    if (volume.bytes_per_sector != BLOCK_SECTOR_SIZE ||
        volume.sectors_per_cluster == 0 ||
        (volume.sectors_per_cluster & (volume.sectors_per_cluster - 1)) ||
        volume.reserved_sectors == 0 || volume.fat_count == 0 ||
        volume.root_entries == 0 || volume.sectors_per_fat == 0 ||
        volume.total_sectors > device->sector_count)
        return false;

    volume.root_sectors = ((uint32_t)volume.root_entries * 32 + 511) / 512;
    volume.fat_lba = volume.reserved_sectors;
    volume.root_lba = volume.fat_lba +
                      (uint32_t)volume.fat_count * volume.sectors_per_fat;
    volume.data_lba = volume.root_lba + volume.root_sectors;
    uint32_t data_sectors = volume.total_sectors - volume.data_lba;
    volume.cluster_count = data_sectors / volume.sectors_per_cluster;

    /* Cluster counts in this range identify FAT16, not FAT12 or FAT32. */
    if (volume.cluster_count < 4085 || volume.cluster_count >= 65525)
        return false;

    return fat16_import_directory(device, &volume, mountpoint, 0, true, 0);
}
