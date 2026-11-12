/* Hierarchical VFS tree with in-memory and block-backed files.
 *
 * A single tree of nodes lives for the duration of the boot session and is
 * shared by every application. Mounted filesystems attach lightweight
 * backends to nodes, so metadata is available immediately while file content
 * can be fetched with fs_read_at() on demand.
 */
#ifndef TINOS_FS_H
#define TINOS_FS_H

#include <tinos/types.h>

#define FS_NAME_MAX 64
#define FS_PATH_MAX 512
#define TINOS_DEFAULT_USER "sammy"
#define TINOS_DEFAULT_HOME "/users/" TINOS_DEFAULT_USER

typedef enum {
    FS_FILE = 1,
    FS_DIR  = 2,
} fs_type_t;

typedef enum {
    FS_OK = 0,
    FS_ERR_NOENT,
    FS_ERR_EXIST,
    FS_ERR_NOTDIR,
    FS_ERR_ISDIR,
    FS_ERR_NOTEMPTY,
    FS_ERR_INVAL,
    FS_ERR_NOMEM,
    FS_ERR_NAMETOOLONG,
    FS_ERR_LOOP,
    FS_ERR_ROOT,
} fs_result_t;

typedef struct fs_node fs_node_t;
typedef bool (*fs_read_at_fn)(const fs_node_t *node, size_t offset,
                              void *buffer, size_t length);
typedef void (*fs_backend_release_fn)(void *backend);
typedef struct fs_io_request fs_io_request_t;
typedef void (*fs_io_complete_fn)(fs_io_request_t *request, void *arg);

struct fs_node {
    char            name[FS_NAME_MAX];
    fs_type_t       type;
    struct fs_node *parent;
    struct fs_node *children; /* directories: first child, name ordered */
    struct fs_node *next;     /* next sibling                           */

    uint8_t *data;     /* regular files: contents (not NUL terminated) */
    size_t   size;
    size_t   capacity; /* owned heap bytes, zero for external contents */
    bool     data_borrowed;
    uint32_t modified; /* fs_clock_ms() at the last modification       */
    void    *backend;
    fs_read_at_fn read_at;
    fs_backend_release_fn backend_release;
};

/* Millisecond clock used for modification times; supplied by the kernel
 * (arch timer) and by the host test harness. */
uint32_t fs_clock_ms(void);

/* Lifecycle */
void       fs_init(void);
void       fs_shutdown(void); /* frees the whole tree (host tests)      */
fs_node_t *fs_root(void);
size_t     fs_node_count(void);
size_t     fs_total_bytes(void);

/* Bumped by every mutation; lets the file explorer notice changes made
 * from a terminal without polling the whole tree. */
uint32_t fs_generation(void);

const char *fs_strerror(fs_result_t err);

/* Lookup */
fs_result_t fs_resolve(fs_node_t *cwd, const char *path, fs_node_t **out);
fs_result_t fs_resolve_parent(fs_node_t *cwd, const char *path,
                              fs_node_t **parent, char *name, size_t name_size);
fs_node_t  *fs_child(const fs_node_t *dir, const char *name);
size_t      fs_child_count(const fs_node_t *dir);
size_t      fs_path(const fs_node_t *node, char *buf, size_t size);
bool        fs_is_ancestor(const fs_node_t *ancestor, const fs_node_t *node);

/* Mutation */
fs_result_t fs_mkdir(fs_node_t *cwd, const char *path, fs_node_t **out);
fs_result_t fs_mkdir_p(fs_node_t *cwd, const char *path, fs_node_t **out);
fs_result_t fs_create(fs_node_t *cwd, const char *path, fs_node_t **out);
fs_result_t fs_touch(fs_node_t *cwd, const char *path, fs_node_t **out);
fs_result_t fs_remove(fs_node_t *cwd, const char *path, bool recursive);
fs_result_t fs_remove_node(fs_node_t *node, bool recursive);
fs_result_t fs_rename(fs_node_t *node, const char *new_name);
fs_result_t fs_move(fs_node_t *cwd, const char *src, const char *dst);
fs_result_t fs_copy(fs_node_t *cwd, const char *src, const char *dst,
                    bool recursive);
fs_result_t fs_copy_node(fs_node_t *src, fs_node_t *dst_dir,
                         const char *new_name, bool recursive,
                         fs_node_t **out);

/* File contents */
/* Attach memory that remains valid for the file's lifetime. Writes detach
 * the file from this storage by copying its contents into the heap. */
fs_result_t fs_bind_external(fs_node_t *file, const void *data, size_t len);
fs_result_t fs_write(fs_node_t *file, const void *data, size_t len);
fs_result_t fs_write_at(fs_node_t *file, size_t offset,
                        const void *data, size_t len);
fs_result_t fs_append(fs_node_t *file, const void *data, size_t len);
fs_result_t fs_write_path(fs_node_t *cwd, const char *path, const void *data,
                          size_t len, bool append);
const uint8_t *fs_data(const fs_node_t *file);
bool fs_read_at(const fs_node_t *file, size_t offset, void *buffer,
                size_t length);
fs_io_request_t *fs_read_all_async(const fs_node_t *file,
                                   fs_io_complete_fn complete, void *arg);
void fs_io_poll(void);
void fs_io_cancel(fs_io_request_t *request);
bool fs_io_ok(const fs_io_request_t *request);
const uint8_t *fs_io_data(const fs_io_request_t *request);
size_t fs_io_size(const fs_io_request_t *request);
void fs_io_release(fs_io_request_t *request);
size_t         fs_size(const fs_node_t *file);

/* Recursive size of a directory (or the file size for a file). */
size_t fs_size_recursive(const fs_node_t *node);

#endif /* TINOS_FS_H */
