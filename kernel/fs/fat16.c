/*
 * Small, read-only FAT16 volume importer.
 *
 * FAT is kept above the block layer. Mounting imports directory metadata into
 * the VFS tree; regular-file contents remain on the block device and are read
 * through the node backend only when an application asks for them.
 */

#include <tinos/fat16.h>
#include <tinos/heap.h>
#include <tinos/kernel.h>
#include <tinos/string.h>
#ifndef TINOS_HOST
#include <tinos/thread.h>
#endif

#define FAT16_MAX_DEPTH 16
#define FAT16_EOC        0xFFF8u
#define FAT16_BAD        0xFFF7u
#define FAT_READ_BATCH   64u

#ifdef TINOS_HOST
#define FAT_READ_CANCELLED() false
#else
#define FAT_READ_CANCELLED() thread_cancelled()
#endif

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

typedef struct {
    block_device_t *device;
    uint32_t fat_lba;
    uint32_t data_lba;
    uint8_t sectors_per_cluster;
    uint32_t cluster_count;
    uint16_t first_cluster;
} fat16_file_backend_t;

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

static bool fat16_file_next(fat16_file_backend_t *file, uint16_t cluster,
                            uint8_t *cache, uint32_t *cache_lba,
                            bool *cache_valid, uint16_t *next)
{
    uint32_t offset = (uint32_t)cluster * 2;
    uint32_t lba = file->fat_lba + offset / BLOCK_SECTOR_SIZE;
    if (!*cache_valid || *cache_lba != lba) {
        if (!block_read(file->device, lba, cache, 1))
            return false;
        *cache_lba = lba;
        *cache_valid = true;
    }
    *next = le16(cache + offset % BLOCK_SECTOR_SIZE);
    return true;
}

static bool fat16_file_batch(fat16_file_backend_t *file, uint16_t cluster,
                             uint32_t sector_index, uint32_t limit,
                             uint8_t *cache, uint32_t *cache_lba,
                             bool *cache_valid, uint32_t *batch)
{
    *batch = MIN(limit, file->sectors_per_cluster - sector_index);
    uint16_t scan = cluster;
    while (*batch < limit) {
        uint16_t next;
        if (!fat16_file_next(file, scan, cache, cache_lba, cache_valid, &next))
            return false;
        if (next != (uint16_t)(scan + 1) ||
            next >= file->cluster_count + 2)
            break;
        scan = next;
        *batch += MIN(limit - *batch,
                      (uint32_t)file->sectors_per_cluster);
    }
    return true;
}

static bool fat16_file_read(const fs_node_t *node, size_t offset,
                            void *buffer, size_t length)
{
    fat16_file_backend_t *file = node->backend;
    uint8_t sector[BLOCK_SECTOR_SIZE];
    uint8_t fat_cache[BLOCK_SECTOR_SIZE];
    uint32_t fat_cache_lba = 0;
    bool fat_cache_valid = false;
    size_t done = 0;
    uint32_t cluster_bytes = (uint32_t)file->sectors_per_cluster *
                             BLOCK_SECTOR_SIZE;
    uint32_t cluster_index = (uint32_t)(offset / cluster_bytes);
    size_t in_cluster = offset % cluster_bytes;
    uint16_t cluster = file->first_cluster;
    uint32_t guard = 0;

    while (cluster_index--) {
        if (FAT_READ_CANCELLED() ||
            cluster < 2 || cluster >= file->cluster_count + 2 ||
            guard++ > file->cluster_count)
            return false;
        if (!fat16_file_next(file, cluster, fat_cache, &fat_cache_lba,
                             &fat_cache_valid, &cluster))
            return false;
    }
    while (done < length) {
        if (FAT_READ_CANCELLED() || cluster < 2 ||
            cluster >= file->cluster_count + 2)
            return false;
        size_t sector_index = in_cluster / BLOCK_SECTOR_SIZE;
        size_t in_sector = in_cluster % BLOCK_SECTOR_SIZE;
        uint32_t lba = file->data_lba +
                       (uint32_t)(cluster - 2) * file->sectors_per_cluster +
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
            uint32_t limit = MIN((uint32_t)FAT_READ_BATCH,
                                 (uint32_t)(remaining / BLOCK_SECTOR_SIZE));
            uint32_t batch;
            if (!fat16_file_batch(file, cluster, (uint32_t)sector_index,
                                  limit, fat_cache, &fat_cache_lba,
                                  &fat_cache_valid, &batch))
                return false;
            if (!block_read(file->device, lba, (uint8_t *)buffer + done,
                            batch))
                return false;
            size_t bytes = (size_t)batch * BLOCK_SECTOR_SIZE;
            done += bytes;
            in_cluster += bytes;
        }
        while (in_cluster >= cluster_bytes && done < length) {
            if (guard++ > file->cluster_count)
                return false;
            if (!fat16_file_next(file, cluster, fat_cache, &fat_cache_lba,
                                 &fat_cache_valid, &cluster))
                return false;
            in_cluster -= cluster_bytes;
        }
    }
    return true;
}

static void fat16_file_release(void *backend)
{
    kfree(backend);
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

    fat16_file_backend_t *backend = kcalloc(1, sizeof(*backend));
    if (!backend)
        return false;
    uint32_t file_size = le32(entry + 28);
    uint16_t first_cluster = le16(entry + 26);
    uint64_t max_bytes = (uint64_t)volume->cluster_count *
                         volume->sectors_per_cluster * BLOCK_SECTOR_SIZE;
    if (file_size > max_bytes || (file_size &&
                                  !fat16_cluster_valid(volume, first_cluster))) {
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
    file->read_at = fat16_file_read;
    file->backend_release = fat16_file_release;
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
    uint8_t *sectors = kmalloc(BLOCK_SECTOR_SIZE * FAT_READ_BATCH);
    if (!sectors)
        return false;
    bool result = false;
    if (root) {
        for (uint32_t s = 0; s < volume->root_sectors;) {
            uint32_t batch = MIN((uint32_t)FAT_READ_BATCH,
                                 volume->root_sectors - s);
            if (!block_read(device, volume->root_lba + s, sectors, batch))
                goto done;
            size_t bytes = (size_t)batch * BLOCK_SECTOR_SIZE;
            for (size_t offset = 0; offset < bytes; offset += 32) {
                if (sectors[offset] == 0x00) {
                    result = true;
                    goto done;
                }
                if (!fat16_import_entry(device, volume, parent,
                                        sectors + offset, depth))
                    goto done;
            }
            s += batch;
        }
        result = true;
        goto done;
    }

    uint16_t cluster = first_cluster;
    uint32_t guard = 0;
    while (fat16_cluster_valid(volume, cluster) &&
           cluster < FAT16_EOC && guard++ <= volume->cluster_count) {
        uint32_t lba = fat16_cluster_lba(volume, cluster);
        for (uint32_t s = 0; s < volume->sectors_per_cluster;) {
            uint32_t batch = MIN((uint32_t)FAT_READ_BATCH,
                                 volume->sectors_per_cluster - s);
            if (!block_read(device, lba + s, sectors, batch))
                goto done;
            size_t bytes = (size_t)batch * BLOCK_SECTOR_SIZE;
            for (size_t offset = 0; offset < bytes; offset += 32) {
                if (sectors[offset] == 0x00) {
                    result = true;
                    goto done;
                }
                if (!fat16_import_entry(device, volume, parent,
                                        sectors + offset, depth))
                    goto done;
            }
            s += batch;
        }
        if (!fat16_next(device, volume, cluster, &cluster))
            goto done;
    }
    result = cluster >= FAT16_EOC;
done:
    kfree(sectors);
    return result;
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
