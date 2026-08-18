/* In-memory filesystem implementation.
 *
 * Nodes form an ordinary tree: every directory owns a name-ordered singly
 * linked list of children.  File contents are a heap buffer that grows
 * geometrically.  There is no on-disk representation - the tree lives for
 * the duration of the boot session, which is exactly what the terminal and
 * the file explorer share.
 *
 * This file is also compiled by the host test harness, so it must stay free
 * of any hardware dependency.
 */

#include <tinos/fs.h>
#include <tinos/heap.h>
#include <tinos/string.h>

static fs_node_t *root;
static size_t     node_count;
static size_t     total_bytes;
static uint32_t   generation;

/* Every structural or content change bumps the generation counter so that
 * open file explorer windows can notice work done from a terminal. */
static void touch_generation(void)
{
    generation++;
}

uint32_t fs_generation(void)
{
    return generation;
}

const char *fs_strerror(fs_result_t err)
{
    switch (err) {
    case FS_OK:              return "success";
    case FS_ERR_NOENT:       return "no such file or directory";
    case FS_ERR_EXIST:       return "file exists";
    case FS_ERR_NOTDIR:      return "not a directory";
    case FS_ERR_ISDIR:       return "is a directory";
    case FS_ERR_NOTEMPTY:    return "directory not empty";
    case FS_ERR_INVAL:       return "invalid argument";
    case FS_ERR_NOMEM:       return "out of memory";
    case FS_ERR_NAMETOOLONG: return "name too long";
    case FS_ERR_LOOP:        return "cannot move a directory into itself";
    case FS_ERR_ROOT:        return "operation not permitted on the root";
    default:                 return "unknown error";
    }
}

/* Node bookkeeping */

static fs_node_t *node_alloc(const char *name, fs_type_t type)
{
    fs_node_t *node = kcalloc(1, sizeof(fs_node_t));
    if (!node)
        return NULL;
    strlcpy(node->name, name, sizeof(node->name));
    node->type     = type;
    node->modified = fs_clock_ms();
    node_count++;
    return node;
}

static void node_free(fs_node_t *node)
{
    if (node->backend_release && node->backend)
        node->backend_release(node->backend);
    if (node->data) {
        total_bytes -= node->capacity;
        kfree(node->data);
    }
    node_count--;
    kfree(node);
}

/* Insert into the parent's child list, keeping it sorted by name. */
static void node_link(fs_node_t *dir, fs_node_t *node)
{
    node->parent   = dir;
    fs_node_t **pp = &dir->children;
    while (*pp && strcmp((*pp)->name, node->name) < 0)
        pp = &(*pp)->next;
    node->next = *pp;
    *pp        = node;
    dir->modified = fs_clock_ms();
    touch_generation();
}

static void node_unlink(fs_node_t *node)
{
    fs_node_t *dir = node->parent;
    if (!dir)
        return;
    for (fs_node_t **pp = &dir->children; *pp; pp = &(*pp)->next) {
        if (*pp == node) {
            *pp = node->next;
            break;
        }
    }
    node->next    = NULL;
    node->parent  = NULL;
    dir->modified = fs_clock_ms();
    touch_generation();
}

static void node_free_tree(fs_node_t *node)
{
    fs_node_t *child = node->children;
    while (child) {
        fs_node_t *next = child->next;
        node_free_tree(child);
        child = next;
    }
    node_free(node);
}

/* Initial tree */

static const char readme_txt[] =
    "Welcome to TinOS!\n"
    "\n"
    "This is an in-memory filesystem: everything you create lives in RAM\n"
    "and disappears when the machine is reset.\n"
    "\n"
    "Try this in the Terminal:\n"
    "  ls /\n"
    "  cd " TINOS_DEFAULT_HOME "\n"
    "  echo hello > greeting.txt\n"
    "  cat greeting.txt\n"
    "  cat greeting.txt | grep hell\n"
    "\n"
    "Then press F5 in the File Explorer to see the new file appear.\n";

static const char help_txt[] =
    "TinOS quick reference\n"
    "======================\n"
    "\n"
    "Desktop\n"
    "  Ctrl+Esc / F12   open or close the start menu\n"
    "  Alt+Tab          switch between windows\n"
    "  Alt+F4           close the focused window\n"
    "  Arrows + Enter   navigate menus and lists\n"
    "\n"
    "Terminal\n"
    "  help             list every built-in command\n"
    "  Up / Down        walk through the command history\n"
    "\n"
    "File Explorer\n"
    "  Enter            open the selected entry\n"
    "  Backspace        go to the parent directory\n"
    "  F2 rename, F5 refresh, Delete remove\n"
    "  Ctrl+C copy, Ctrl+V paste, Ctrl+N new folder, Ctrl+T new file\n";

static const char motd_txt[] =
    "TinOS 0.1.0\n"
    "A tiny, fast graphical operating system.\n"
    "Use the start menu to open apps, and read /README.txt to get started.\n";

static void seed_file(fs_node_t *dir, const char *name, const char *text)
{
    fs_node_t *file = node_alloc(name, FS_FILE);
    if (!file)
        return;
    node_link(dir, file);
    fs_write(file, text, strlen(text));
}

static fs_node_t *seed_dir(fs_node_t *parent, const char *name)
{
    fs_node_t *dir = node_alloc(name, FS_DIR);
    if (!dir)
        return NULL;
    node_link(parent, dir);
    return dir;
}

void fs_init(void)
{
    root = node_alloc("/", FS_DIR);
    if (!root)
        return;

    fs_node_t *apps    = seed_dir(root, "apps");
    fs_node_t *config  = seed_dir(root, "config");
    fs_node_t *dev     = seed_dir(root, "dev");
    fs_node_t *drive   = seed_dir(root, "drive");
    fs_node_t *run     = seed_dir(root, "run");
    fs_node_t *system  = seed_dir(root, "system");
    fs_node_t *tmp     = seed_dir(root, "tmp");
    fs_node_t *var     = seed_dir(root, "var");
    fs_node_t *users   = seed_dir(root, "users");
    fs_node_t *shared  = seed_dir(root, "shared");

    (void)apps;
    (void)config;
    (void)dev;
    (void)drive;
    (void)run;
    (void)tmp;
    (void)shared;

    seed_dir(system, "bin");
    seed_dir(system, "drivers");
    seed_dir(system, "lib");
    seed_dir(var, "cache");
    seed_dir(var, "lib");
    seed_dir(var, "log");

    fs_node_t *user = seed_dir(users, TINOS_DEFAULT_USER);
    fs_node_t *apps_user = seed_dir(user, ".apps");
    fs_node_t *data_user = seed_dir(user, ".data");
    seed_dir(user, "documents");
    seed_dir(user, "downloads");

    (void)apps_user;
    (void)data_user;

    seed_file(root, "README.txt", readme_txt);
    seed_file(system, "motd.txt", motd_txt);
    seed_file(system, "help.txt", help_txt);
}

void fs_shutdown(void)
{
    if (root)
        node_free_tree(root);
    root        = NULL;
    node_count  = 0;
    total_bytes = 0;
}

fs_node_t *fs_root(void)
{
    return root;
}

size_t fs_node_count(void)
{
    return node_count;
}

size_t fs_total_bytes(void)
{
    return total_bytes;
}

/* Lookup */

fs_node_t *fs_child(const fs_node_t *dir, const char *name)
{
    if (!dir || dir->type != FS_DIR)
        return NULL;
    for (fs_node_t *c = dir->children; c; c = c->next)
        if (strcmp(c->name, name) == 0)
            return c;
    return NULL;
}

size_t fs_child_count(const fs_node_t *dir)
{
    size_t n = 0;
    if (!dir || dir->type != FS_DIR)
        return 0;
    for (fs_node_t *c = dir->children; c; c = c->next)
        n++;
    return n;
}

bool fs_is_ancestor(const fs_node_t *ancestor, const fs_node_t *node)
{
    for (const fs_node_t *n = node; n; n = n->parent)
        if (n == ancestor)
            return true;
    return false;
}

/* Copy the next path component into `out`, returning the position after it. */
static const char *next_component(const char *p, char *out, size_t out_size,
                                  bool *too_long)
{
    size_t n = 0;
    *too_long = false;
    while (*p && *p != '/') {
        if (n + 1 < out_size)
            out[n] = *p;
        else
            *too_long = true;
        n++;
        p++;
    }
    out[n < out_size ? n : out_size - 1] = '\0';
    while (*p == '/')
        p++;
    return p;
}

fs_result_t fs_resolve(fs_node_t *cwd, const char *path, fs_node_t **out)
{
    if (!path || !root)
        return FS_ERR_INVAL;

    fs_node_t *node = (*path == '/') ? root : (cwd ? cwd : root);
    const char *p   = path;
    while (*p == '/')
        p++;

    char component[FS_NAME_MAX];
    while (*p) {
        bool too_long;
        p = next_component(p, component, sizeof(component), &too_long);
        if (too_long)
            return FS_ERR_NAMETOOLONG;
        if (component[0] == '\0' || strcmp(component, ".") == 0)
            continue;
        if (strcmp(component, "..") == 0) {
            node = node->parent ? node->parent : root;
            continue;
        }
        if (node->type != FS_DIR)
            return FS_ERR_NOTDIR;
        fs_node_t *child = fs_child(node, component);
        if (!child)
            return FS_ERR_NOENT;
        node = child;
    }

    if (out)
        *out = node;
    return FS_OK;
}

fs_result_t fs_resolve_parent(fs_node_t *cwd, const char *path,
                              fs_node_t **parent, char *name, size_t name_size)
{
    if (!path || !root)
        return FS_ERR_INVAL;

    /* Trim trailing slashes so "a/b/" behaves like "a/b". */
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/')
        len--;
    if (len == 0 || (len == 1 && path[0] == '/'))
        return FS_ERR_ROOT;

    /* Split at the final separator. */
    size_t split = len;
    while (split > 0 && path[split - 1] != '/')
        split--;

    const char *leaf     = path + split;
    size_t      leaf_len = len - split;
    if (leaf_len == 0)
        return FS_ERR_INVAL;
    if (leaf_len >= name_size || leaf_len >= FS_NAME_MAX)
        return FS_ERR_NAMETOOLONG;

    char leaf_buf[FS_NAME_MAX];
    memcpy(leaf_buf, leaf, leaf_len);
    leaf_buf[leaf_len] = '\0';

    fs_node_t *dir = NULL;
    if (split == 0) {
        dir = cwd ? cwd : root;
    } else {
        char prefix[FS_PATH_MAX];
        size_t plen = split;
        if (plen >= sizeof(prefix))
            return FS_ERR_NAMETOOLONG;
        memcpy(prefix, path, plen);
        prefix[plen] = '\0';
        fs_result_t r = fs_resolve(cwd, prefix, &dir);
        if (r != FS_OK)
            return r;
    }

    /* "." and ".." as the final component refer to a directory, not a name. */
    if (strcmp(leaf_buf, ".") == 0 || strcmp(leaf_buf, "..") == 0)
        return FS_ERR_INVAL;
    if (dir->type != FS_DIR)
        return FS_ERR_NOTDIR;

    strlcpy(name, leaf_buf, name_size);
    *parent = dir;
    return FS_OK;
}

size_t fs_path(const fs_node_t *node, char *buf, size_t size)
{
    if (!node || size == 0)
        return 0;

    /* Walk up to the root collecting names, then emit them in order. */
    const fs_node_t *stack[64];
    int              depth = 0;
    for (const fs_node_t *n = node; n && n->parent && depth < 64; n = n->parent)
        stack[depth++] = n;

    size_t pos = 0;
    buf[pos++] = '/';
    for (int i = depth - 1; i >= 0; i--) {
        size_t nlen = strlen(stack[i]->name);
        if (pos + nlen + 2 >= size)
            break;
        memcpy(buf + pos, stack[i]->name, nlen);
        pos += nlen;
        if (i > 0)
            buf[pos++] = '/';
    }
    buf[pos] = '\0';
    return pos;
}

/* Creation and removal */

static fs_result_t validate_name(const char *name)
{
    if (name[0] == '\0')
        return FS_ERR_INVAL;
    if (strlen(name) >= FS_NAME_MAX)
        return FS_ERR_NAMETOOLONG;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return FS_ERR_INVAL;
    if (strchr(name, '/'))
        return FS_ERR_INVAL;
    return FS_OK;
}

static fs_result_t create_at(fs_node_t *cwd, const char *path, fs_type_t type,
                             fs_node_t **out)
{
    fs_node_t *dir;
    char       name[FS_NAME_MAX];

    fs_result_t r = fs_resolve_parent(cwd, path, &dir, name, sizeof(name));
    if (r != FS_OK)
        return r;
    r = validate_name(name);
    if (r != FS_OK)
        return r;
    if (fs_child(dir, name))
        return FS_ERR_EXIST;

    fs_node_t *node = node_alloc(name, type);
    if (!node)
        return FS_ERR_NOMEM;
    node_link(dir, node);
    if (out)
        *out = node;
    return FS_OK;
}

fs_result_t fs_mkdir(fs_node_t *cwd, const char *path, fs_node_t **out)
{
    return create_at(cwd, path, FS_DIR, out);
}

fs_result_t fs_create(fs_node_t *cwd, const char *path, fs_node_t **out)
{
    return create_at(cwd, path, FS_FILE, out);
}

fs_result_t fs_mkdir_p(fs_node_t *cwd, const char *path, fs_node_t **out)
{
    if (!path || !root)
        return FS_ERR_INVAL;

    fs_node_t  *node = (*path == '/') ? root : (cwd ? cwd : root);
    const char *p    = path;
    while (*p == '/')
        p++;

    char component[FS_NAME_MAX];
    while (*p) {
        bool too_long;
        p = next_component(p, component, sizeof(component), &too_long);
        if (too_long)
            return FS_ERR_NAMETOOLONG;
        if (component[0] == '\0' || strcmp(component, ".") == 0)
            continue;
        if (strcmp(component, "..") == 0) {
            node = node->parent ? node->parent : root;
            continue;
        }
        if (node->type != FS_DIR)
            return FS_ERR_NOTDIR;

        fs_node_t *child = fs_child(node, component);
        if (!child) {
            child = node_alloc(component, FS_DIR);
            if (!child)
                return FS_ERR_NOMEM;
            node_link(node, child);
        }
        node = child;
    }
    if (out)
        *out = node;
    return FS_OK;
}

fs_result_t fs_touch(fs_node_t *cwd, const char *path, fs_node_t **out)
{
    fs_node_t  *node;
    fs_result_t r = fs_resolve(cwd, path, &node);
    if (r == FS_OK) {
        node->modified = fs_clock_ms();
        if (out)
            *out = node;
        return FS_OK;
    }
    if (r != FS_ERR_NOENT)
        return r;
    return fs_create(cwd, path, out);
}

fs_result_t fs_remove_node(fs_node_t *node, bool recursive)
{
    if (!node)
        return FS_ERR_NOENT;
    if (node == root)
        return FS_ERR_ROOT;
    if (node->type == FS_DIR && node->children && !recursive)
        return FS_ERR_NOTEMPTY;

    node_unlink(node);
    node_free_tree(node);
    return FS_OK;
}

fs_result_t fs_remove(fs_node_t *cwd, const char *path, bool recursive)
{
    fs_node_t  *node;
    fs_result_t r = fs_resolve(cwd, path, &node);
    if (r != FS_OK)
        return r;
    return fs_remove_node(node, recursive);
}

fs_result_t fs_rename(fs_node_t *node, const char *new_name)
{
    if (!node)
        return FS_ERR_NOENT;
    if (node == root)
        return FS_ERR_ROOT;
    fs_result_t r = validate_name(new_name);
    if (r != FS_OK)
        return r;
    if (strcmp(node->name, new_name) == 0)
        return FS_OK;
    if (fs_child(node->parent, new_name))
        return FS_ERR_EXIST;

    fs_node_t *dir = node->parent;
    node_unlink(node);
    strlcpy(node->name, new_name, sizeof(node->name));
    node_link(dir, node);
    node->modified = fs_clock_ms();
    return FS_OK;
}

/* Move and copy */

fs_result_t fs_move(fs_node_t *cwd, const char *src, const char *dst)
{
    fs_node_t  *node;
    fs_result_t r = fs_resolve(cwd, src, &node);
    if (r != FS_OK)
        return r;
    if (node == root)
        return FS_ERR_ROOT;

    fs_node_t *target_dir = NULL;
    char       new_name[FS_NAME_MAX];

    fs_node_t *existing = NULL;
    if (fs_resolve(cwd, dst, &existing) == FS_OK && existing->type == FS_DIR) {
        /* Moving into an existing directory keeps the original name. */
        target_dir = existing;
        strlcpy(new_name, node->name, sizeof(new_name));
        if (fs_child(target_dir, new_name) == node)
            return FS_OK; /* already there */
    } else {
        r = fs_resolve_parent(cwd, dst, &target_dir, new_name,
                              sizeof(new_name));
        if (r != FS_OK)
            return r;
        r = validate_name(new_name);
        if (r != FS_OK)
            return r;
    }

    if (fs_is_ancestor(node, target_dir))
        return FS_ERR_LOOP;

    fs_node_t *clash = fs_child(target_dir, new_name);
    if (clash && clash != node) {
        if (clash->type == FS_DIR)
            return FS_ERR_EXIST; /* never silently replace a directory */
        if (node->type == FS_DIR)
            return FS_ERR_EXIST;
        fs_remove_node(clash, false); /* overwrite a plain file */
    }

    node_unlink(node);
    strlcpy(node->name, new_name, sizeof(node->name));
    node_link(target_dir, node);
    node->modified = fs_clock_ms();
    return FS_OK;
}

fs_result_t fs_copy_node(fs_node_t *src, fs_node_t *dst_dir,
                         const char *new_name, bool recursive,
                         fs_node_t **out)
{
    if (!src || !dst_dir || dst_dir->type != FS_DIR)
        return FS_ERR_INVAL;
    fs_result_t r = validate_name(new_name);
    if (r != FS_OK)
        return r;
    if (src->type == FS_DIR && !recursive)
        return FS_ERR_ISDIR;
    if (src->type == FS_DIR && fs_is_ancestor(src, dst_dir))
        return FS_ERR_LOOP;

    fs_node_t *clash = fs_child(dst_dir, new_name);
    if (clash) {
        if (clash == src)
            return FS_ERR_EXIST;
        if (clash->type == FS_DIR || src->type == FS_DIR)
            return FS_ERR_EXIST;
        fs_remove_node(clash, false);
    }

    fs_node_t *copy = node_alloc(new_name, src->type);
    if (!copy)
        return FS_ERR_NOMEM;
    node_link(dst_dir, copy);

    if (src->type == FS_FILE) {
        const uint8_t *contents = fs_data(src);
        r = contents ? fs_write(copy, contents, src->size) : FS_ERR_NOMEM;
        if (r != FS_OK) {
            fs_remove_node(copy, false);
            return r;
        }
    } else {
        for (fs_node_t *child = src->children; child; child = child->next) {
            r = fs_copy_node(child, copy, child->name, true, NULL);
            if (r != FS_OK) {
                fs_remove_node(copy, true);
                return r;
            }
        }
    }
    if (out)
        *out = copy;
    return FS_OK;
}

fs_result_t fs_copy(fs_node_t *cwd, const char *src, const char *dst,
                    bool recursive)
{
    fs_node_t  *node;
    fs_result_t r = fs_resolve(cwd, src, &node);
    if (r != FS_OK)
        return r;
    if (node == root)
        return FS_ERR_ROOT;

    fs_node_t *target_dir = NULL;
    char       new_name[FS_NAME_MAX];
    fs_node_t *existing = NULL;

    if (fs_resolve(cwd, dst, &existing) == FS_OK && existing->type == FS_DIR) {
        target_dir = existing;
        strlcpy(new_name, node->name, sizeof(new_name));
    } else {
        r = fs_resolve_parent(cwd, dst, &target_dir, new_name,
                              sizeof(new_name));
        if (r != FS_OK)
            return r;
    }
    if (target_dir == node->parent && strcmp(new_name, node->name) == 0)
        return FS_ERR_EXIST; /* copying a file onto itself */

    return fs_copy_node(node, target_dir, new_name, recursive, NULL);
}

/* File contents */

static fs_result_t ensure_capacity(fs_node_t *file, size_t needed)
{
    if (file->capacity >= needed)
        return FS_OK;

    size_t capacity = file->capacity ? file->capacity : 64;
    while (capacity < needed)
        capacity *= 2;

    uint8_t *buf = krealloc(file->data, capacity);
    if (!buf)
        return FS_ERR_NOMEM;
    total_bytes += capacity - file->capacity;
    file->data     = buf;
    file->capacity = capacity;
    return FS_OK;
}

fs_result_t fs_write(fs_node_t *file, const void *data, size_t len)
{
    if (!file)
        return FS_ERR_NOENT;
    if (file->type != FS_FILE)
        return FS_ERR_ISDIR;
    if (file->backend)
        return FS_ERR_INVAL;

    if (len == 0) {
        file->size     = 0;
        file->modified = fs_clock_ms();
        touch_generation();
        return FS_OK;
    }
    fs_result_t r = ensure_capacity(file, len);
    if (r != FS_OK)
        return r;
    memcpy(file->data, data, len);
    file->size     = len;
    file->modified = fs_clock_ms();
    touch_generation();
    return FS_OK;
}

fs_result_t fs_append(fs_node_t *file, const void *data, size_t len)
{
    if (!file)
        return FS_ERR_NOENT;
    if (file->type != FS_FILE)
        return FS_ERR_ISDIR;
    if (file->backend)
        return FS_ERR_INVAL;
    if (len == 0)
        return FS_OK;

    fs_result_t r = ensure_capacity(file, file->size + len);
    if (r != FS_OK)
        return r;
    memcpy(file->data + file->size, data, len);
    file->size += len;
    file->modified = fs_clock_ms();
    touch_generation();
    return FS_OK;
}

fs_result_t fs_write_path(fs_node_t *cwd, const char *path, const void *data,
                          size_t len, bool append)
{
    fs_node_t  *file = NULL;
    fs_result_t r    = fs_resolve(cwd, path, &file);

    if (r == FS_ERR_NOENT) {
        r = fs_create(cwd, path, &file);
        if (r != FS_OK)
            return r;
    } else if (r != FS_OK) {
        return r;
    }
    if (file->type != FS_FILE)
        return FS_ERR_ISDIR;

    return append ? fs_append(file, data, len) : fs_write(file, data, len);
}

const uint8_t *fs_data(const fs_node_t *file)
{
    if (!file || file->type != FS_FILE)
        return NULL;
    if (!file->data && file->backend && file->size) {
        fs_node_t *mutable_file = (fs_node_t *)file;
        uint8_t *loaded = kmalloc(file->size);
        if (!loaded || !file->read_at(file, 0, loaded, file->size)) {
            if (loaded)
                kfree(loaded);
            return NULL;
        }
        mutable_file->data = loaded;
        mutable_file->capacity = file->size;
        total_bytes += file->size;
    }
    return file->data;
}

bool fs_read_at(const fs_node_t *file, size_t offset, void *buffer,
                size_t length)
{
    if (!file || file->type != FS_FILE || (!buffer && length) ||
        offset > file->size || length > file->size - offset)
        return false;
    if (!length)
        return true;
    if (file->data) {
        memcpy(buffer, file->data + offset, length);
        return true;
    }
    return file->read_at && file->read_at(file, offset, buffer, length);
}

size_t fs_size(const fs_node_t *file)
{
    return file ? file->size : 0;
}

size_t fs_size_recursive(const fs_node_t *node)
{
    if (!node)
        return 0;
    if (node->type == FS_FILE)
        return node->size;
    size_t total = 0;
    for (fs_node_t *c = node->children; c; c = c->next)
        total += fs_size_recursive(c);
    return total;
}
