/* Read-only, metadata-only FAT32 mount. See fat16.c for the shared design. */

#include <tinos/fat32.h>
#include <tinos/heap.h>
#include <tinos/kernel.h>
#include <tinos/string.h>

#define FAT32_MAX_DEPTH 16
#define FAT32_EOC        0x0FFFFFF8u
#define FAT32_BAD        0x0FFFFFF7u
#define FAT_READ_BATCH   64u

typedef struct {
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint32_t sectors_per_fat;
    uint32_t total_sectors;
    uint32_t fat_lba;
    uint32_t data_lba;
    uint32_t root_cluster;
    uint32_t cluster_count;
    uint32_t fat_cache_lba;
    bool     fat_cache_valid;
    uint8_t  fat_cache[BLOCK_SECTOR_SIZE];
} fat32_volume_t;

typedef struct {
    block_device_t *device;
    uint32_t fat_lba;
    uint32_t data_lba;
    uint8_t sectors_per_cluster;
    uint32_t cluster_count;
    uint32_t first_cluster;
} fat32_file_backend_t;

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)le16(p) | ((uint32_t)le16(p + 2) << 16);
}

static bool sector_read(block_device_t *device, uint32_t lba, uint8_t *buffer)
{
    return block_read(device, lba, buffer, 1);
}

static bool next_cluster(block_device_t *device, fat32_volume_t *volume,
                         uint32_t cluster, uint32_t *next)
{
    uint32_t offset = cluster * 4;
    uint32_t lba = volume->fat_lba + offset / BLOCK_SECTOR_SIZE;
    if (!volume->fat_cache_valid || volume->fat_cache_lba != lba) {
        if (!sector_read(device, lba, volume->fat_cache)) return false;
        volume->fat_cache_lba = lba;
        volume->fat_cache_valid = true;
    }
    *next = le32(volume->fat_cache + offset % BLOCK_SECTOR_SIZE) & 0x0FFFFFFF;
    return true;
}

static bool cluster_valid(const fat32_volume_t *volume, uint32_t cluster)
{
    return cluster >= 2 && cluster < volume->cluster_count + 2;
}

static uint32_t cluster_lba(const fat32_volume_t *volume, uint32_t cluster)
{
    return volume->data_lba + (cluster - 2) * volume->sectors_per_cluster;
}

static bool fat32_file_next(fat32_file_backend_t *file, uint32_t cluster,
                            uint8_t *cache, uint32_t *cache_lba,
                            bool *cache_valid, uint32_t *next)
{
    uint32_t offset = cluster * 4;
    uint32_t lba = file->fat_lba + offset / BLOCK_SECTOR_SIZE;
    if (!*cache_valid || *cache_lba != lba) {
        if (!block_read(file->device, lba, cache, 1))
            return false;
        *cache_lba = lba;
        *cache_valid = true;
    }
    *next = le32(cache + offset % BLOCK_SECTOR_SIZE) & 0x0FFFFFFF;
    return true;
}

static bool fat32_file_read(const fs_node_t *node, size_t offset,
                            void *buffer, size_t length)
{
    fat32_file_backend_t *file = node->backend;
    uint8_t sector[BLOCK_SECTOR_SIZE];
    uint8_t fat_cache[BLOCK_SECTOR_SIZE];
    uint32_t fat_cache_lba = 0;
    bool fat_cache_valid = false;
    size_t done = 0;
    uint32_t cluster_bytes = (uint32_t)file->sectors_per_cluster *
                             BLOCK_SECTOR_SIZE;
    uint32_t cluster_index = (uint32_t)(offset / cluster_bytes);
    size_t in_cluster = offset % cluster_bytes;
    uint32_t cluster = file->first_cluster;
    uint32_t guard = 0;

    while (cluster_index--) {
        if (cluster < 2 || cluster >= file->cluster_count + 2 ||
            guard++ > file->cluster_count ||
            !fat32_file_next(file, cluster, fat_cache, &fat_cache_lba,
                             &fat_cache_valid, &cluster))
            return false;
    }
    while (done < length) {
        if (cluster < 2 || cluster >= file->cluster_count + 2 ||
            cluster == FAT32_BAD || cluster >= FAT32_EOC)
            return false;
        size_t sector_index = in_cluster / BLOCK_SECTOR_SIZE;
        size_t in_sector = in_cluster % BLOCK_SECTOR_SIZE;
        uint32_t lba = file->data_lba +
                       (cluster - 2) * file->sectors_per_cluster +
                       (uint32_t)sector_index;
        size_t remaining = length - done;
        if (in_sector || remaining < BLOCK_SECTOR_SIZE) {
            if (!block_read(file->device, lba, sector, 1))
                return false;
            size_t count = MIN(remaining, BLOCK_SECTOR_SIZE - in_sector);
            memcpy((uint8_t *)buffer + done, sector + in_sector, count);
            done += count;
            in_cluster += count;
        } else {
            uint32_t batch = MIN((uint32_t)FAT_READ_BATCH,
                                 file->sectors_per_cluster -
                                 (uint32_t)sector_index);
            batch = MIN(batch, (uint32_t)(remaining / BLOCK_SECTOR_SIZE));
            if (!block_read(file->device, lba, (uint8_t *)buffer + done,
                            batch))
                return false;
            size_t bytes = (size_t)batch * BLOCK_SECTOR_SIZE;
            done += bytes;
            in_cluster += bytes;
        }
        if (in_cluster == cluster_bytes && done < length) {
            if (guard++ > file->cluster_count)
                return false;
            if (!fat32_file_next(file, cluster, fat_cache, &fat_cache_lba,
                                 &fat_cache_valid, &cluster))
                return false;
            in_cluster = 0;
        }
    }
    return true;
}

static void fat32_file_release(void *backend)
{
    kfree(backend);
}

static void short_name(const uint8_t *entry, char *name, size_t capacity)
{
    size_t used = 0;
    for (size_t i = 0; i < 8 && entry[i] != ' '; i++)
        if (used + 1 < capacity)
            name[used++] = (char)entry[i];
    size_t extension = 0;
    for (size_t i = 0; i < 3 && entry[8 + i] != ' '; i++)
        extension++;
    if (extension && used + 1 < capacity)
        name[used++] = '.';
    for (size_t i = 0; i < extension && used + 1 < capacity; i++)
        name[used++] = (char)entry[8 + i];
    name[used] = '\0';
}

static bool import_directory(block_device_t *, fat32_volume_t *,
                             fs_node_t *, uint32_t, unsigned);

static bool import_file(block_device_t *device, fat32_volume_t *volume,
                        fs_node_t *parent, const uint8_t *entry,
                        const char *name)
{
    fs_node_t *file = NULL;
    if (fs_create(parent, name, &file) != FS_OK)
        return false;
    fat32_file_backend_t *backend = kcalloc(1, sizeof(*backend));
    if (!backend)
        return false;
    uint32_t file_size = le32(entry + 28);
    uint32_t first_cluster = ((uint32_t)le16(entry + 20) << 16) |
                             le16(entry + 26);
    uint64_t max_bytes = (uint64_t)volume->cluster_count *
                         volume->sectors_per_cluster * BLOCK_SECTOR_SIZE;
    if (file_size > max_bytes || (file_size &&
                                  !cluster_valid(volume, first_cluster))) {
        kfree(backend);
        return false;
    }
    backend->device = device;
    backend->fat_lba = volume->fat_lba;
    backend->data_lba = volume->data_lba;
    backend->sectors_per_cluster = volume->sectors_per_cluster;
    backend->cluster_count = volume->cluster_count;
    backend->first_cluster = first_cluster;
    file->size = file_size;
    file->backend = backend;
    file->read_at = fat32_file_read;
    file->backend_release = fat32_file_release;
    return true;
}

static bool import_entry(block_device_t *device, fat32_volume_t *volume,
                         fs_node_t *parent, const uint8_t *entry,
                         unsigned depth)
{
    if (entry[0] == 0x00 || entry[0] == 0xE5 || entry[11] == 0x0F ||
        (entry[11] & 0x08))
        return true;
    char name[FS_NAME_MAX];
    short_name(entry, name, sizeof(name));
    if (!name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return true;
    uint32_t cluster = ((uint32_t)le16(entry + 20) << 16) | le16(entry + 26);
    if (entry[11] & 0x10) {
        if (depth >= FAT32_MAX_DEPTH)
            return false;
        fs_node_t *child = NULL;
        if (fs_mkdir(parent, name, &child) != FS_OK)
            return false;
        return import_directory(device, volume, child, cluster, depth + 1);
    }
    return import_file(device, volume, parent, entry, name);
}

static bool import_directory(block_device_t *device,
                             fat32_volume_t *volume, fs_node_t *parent,
                             uint32_t first_cluster, unsigned depth)
{
    uint32_t cluster = first_cluster;
    uint32_t guard = 0;
    uint8_t sector[BLOCK_SECTOR_SIZE];
    while (cluster_valid(volume, cluster) && cluster < FAT32_EOC &&
           guard++ <= volume->cluster_count) {
        uint32_t lba = cluster_lba(volume, cluster);
        for (uint32_t s = 0; s < volume->sectors_per_cluster; s++) {
            if (!sector_read(device, lba + s, sector))
                return false;
            for (size_t offset = 0; offset < BLOCK_SECTOR_SIZE; offset += 32) {
                if (sector[offset] == 0x00)
                    return true;
                if (!import_entry(device, volume, parent, sector + offset,
                                  depth))
                    return false;
            }
        }
        if (!next_cluster(device, volume, cluster, &cluster))
            return false;
    }
    return cluster >= FAT32_EOC;
}

bool fat32_import(block_device_t *device, fs_node_t *mountpoint)
{
    if (!device || !mountpoint || mountpoint->type != FS_DIR)
        return false;
    uint8_t boot[BLOCK_SECTOR_SIZE];
    if (!sector_read(device, 0, boot) || le16(boot + 510) != 0xAA55)
        return false;

    fat32_volume_t volume;
    uint16_t bytes_per_sector = le16(boot + 11);
    volume.sectors_per_cluster = boot[13];
    volume.reserved_sectors = le16(boot + 14);
    volume.fat_count = boot[16];
    volume.sectors_per_fat = le32(boot + 36);
    volume.total_sectors = le16(boot + 19);
    if (!volume.total_sectors)
        volume.total_sectors = le32(boot + 32);
    volume.root_cluster = le32(boot + 44);
    volume.fat_lba = volume.reserved_sectors;
    volume.data_lba = volume.fat_lba + volume.fat_count * volume.sectors_per_fat;

    if (bytes_per_sector != BLOCK_SECTOR_SIZE ||
        volume.sectors_per_cluster == 0 ||
        (volume.sectors_per_cluster & (volume.sectors_per_cluster - 1)) ||
        volume.reserved_sectors == 0 || volume.fat_count == 0 ||
        volume.sectors_per_fat == 0 || volume.root_cluster < 2 ||
        volume.total_sectors > device->sector_count ||
        volume.data_lba >= volume.total_sectors)
        return false;

    volume.cluster_count = (volume.total_sectors - volume.data_lba) /
                           volume.sectors_per_cluster;
    if (volume.cluster_count < 65525)
        return false;
    bool imported = import_directory(device, &volume, mountpoint,
                                     volume.root_cluster, 0);
    if (!imported)
        klog("[fat32] directory import failed root=%u data=%u clusters=%u\n",
             volume.root_cluster, volume.data_lba, volume.cluster_count);
    return imported;
}
