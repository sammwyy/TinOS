/* Shared volume probing and /drive mount naming. */

#include <tinos/fat16.h>
#include <tinos/fat32.h>
#include <tinos/fs.h>
#include <tinos/mbr.h>
#include <tinos/printf.h>
#include <tinos/storage.h>

static bool mount_volume(block_device_t *device, const char *path)
{
    fs_node_t *mountpoint = NULL;
    if (fs_mkdir(fs_root(), path, &mountpoint) != FS_OK)
        return false;
    if (fat16_import(device, mountpoint) || fat32_import(device, mountpoint))
        return true;
    fs_remove(fs_root(), path, true);
    return false;
}

size_t storage_mount_device(block_device_t *device, size_t *mount_index)
{
    if (!device || !mount_index)
        return 0;

    size_t mounted = 0;
    char path[FS_PATH_MAX];
    ksnprintf(path, sizeof(path), "/drive/disk%u", (uint32_t)*mount_index);
    if (mount_volume(device, path)) {
        klog("[fat ] mounted %s at %s\n", device->name, path);
        (*mount_index)++;
        return 1;
    }

    block_device_t partitions[MBR_MAX_PARTITIONS];
    size_t count = mbr_partitions(device, partitions, ARRAY_SIZE(partitions));
    for (size_t i = 0; i < count; i++) {
        ksnprintf(path, sizeof(path), "/drive/disk%u", (uint32_t)*mount_index);
        if (!mount_volume(&partitions[i], path))
            continue;
        klog("[fat ] mounted %s at %s\n", partitions[i].name, path);
        (*mount_index)++;
        mounted++;
    }
    return mounted;
}
