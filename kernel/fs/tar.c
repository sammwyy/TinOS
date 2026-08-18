#include <tinos/fs.h>
#include <tinos/kernel.h>
#include <tinos/printf.h>
#include <tinos/string.h>
#include <tinos/tar.h>

#define TAR_BLOCK_SIZE 512

typedef struct PACKED {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
} tar_header_t;

#define TAR_REGTYPE  '0'
#define TAR_AREGTYPE '\0'
#define TAR_DIRTYPE  '5'

static size_t octal_to_size(const char *field, size_t width)
{
    size_t value = 0;
    for (size_t i = 0; i < width && field[i] >= '0' && field[i] <= '7'; i++)
        value = value * 8 + (size_t)(field[i] - '0');
    return value;
}

static void split_dirname(const char *path, char *out, size_t out_size)
{
    const char *slash = strrchr(path, '/');
    if (!slash) {
        out[0] = '\0';
        return;
    }
    size_t len = (size_t)(slash - path);
    if (len >= out_size)
        len = out_size - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

size_t tar_extract(const void *image, size_t size, const char *dest_dir)
{
    fs_node_t *root = fs_root();
    fs_mkdir_p(root, dest_dir, NULL);

    size_t files    = 0;
    size_t offset   = 0;
    while (offset + TAR_BLOCK_SIZE <= size) {
        const tar_header_t *hdr = (const tar_header_t *)((const uint8_t *)image + offset);
        offset += TAR_BLOCK_SIZE;
        if (hdr->name[0] == '\0')
            break; /* end-of-archive marker */

        size_t entry_size = octal_to_size(hdr->size, sizeof(hdr->size));
        char   path[FS_PATH_MAX];
        ksnprintf(path, sizeof(path), "%s/%.100s", dest_dir, hdr->name);

        if (hdr->typeflag == TAR_DIRTYPE) {
            fs_mkdir_p(root, path, NULL);
        } else if (hdr->typeflag == TAR_REGTYPE || hdr->typeflag == TAR_AREGTYPE) {
            if (offset + entry_size > size)
                break;
            char parent[FS_PATH_MAX];
            split_dirname(path, parent, sizeof(parent));
            if (parent[0])
                fs_mkdir_p(root, parent, NULL);
            if (fs_write_path(root, path, (const uint8_t *)image + offset,
                              entry_size, false) == FS_OK)
                files++;
        }

        offset += ALIGN_UP(entry_size, TAR_BLOCK_SIZE);
    }
    return files;
}
