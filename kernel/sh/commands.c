/* Built-in commands.
 *
 * Every command has the same shape: it reads bytes from `in`, writes its
 * result to `out` and its diagnostics to `err`, and returns an exit status.
 * That uniformity is what makes pipelines, redirection and the host unit
 * tests work without any special cases.
 */

#include <tinos/heap.h>
#include <tinos/path.h>
#include <tinos/printf.h>
#include <tinos/net.h>
#include <tinos/regex.h>
#include <tinos/sh.h>
#include <tinos/string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void report(stream_t *err, const char *cmd, const char *path,
                   fs_result_t r)
{
    if (path)
        stream_printf(err, "%s: %s: %s\n", cmd, path, fs_strerror(r));
    else
        stream_printf(err, "%s: %s\n", cmd, fs_strerror(r));
}

static void usage_error(stream_t *err, const char *cmd)
{
    const sh_command_def_t *def = sh_find_command(cmd);
    if (def)
        stream_printf(err, "usage: %s\n", def->usage);
}

/* Option parsing: returns the index of the first non-option argument and
 * fills `flags` with the letters seen.  "--" ends the option list. */
static int parse_flags(int argc, char **argv, const char *allowed,
                       char *flags, size_t flags_size, stream_t *err,
                       bool *ok)
{
    size_t nflags = 0;
    int    i      = 1;
    *ok           = true;

    for (; i < argc; i++) {
        const char *arg = argv[i];
        if (arg[0] != '-' || arg[1] == '\0')
            break;
        if (strcmp(arg, "--") == 0) {
            i++;
            break;
        }
        for (const char *c = arg + 1; *c; c++) {
            if (!strchr(allowed, *c)) {
                stream_printf(err, "%s: unknown option -- '%c'\n", argv[0], *c);
                usage_error(err, argv[0]);
                *ok = false;
                return i;
            }
            if (nflags + 1 < flags_size)
                flags[nflags++] = *c;
        }
    }
    flags[nflags] = '\0';
    return i;
}

static bool has_flag(const char *flags, char f)
{
    return strchr(flags, f) != NULL;
}

/* Iterate over the lines of a buffer (the final line may lack a newline). */
typedef struct {
    const char *p;
    const char *end;
} line_iter_t;

static void line_iter_init(line_iter_t *it, const char *data, size_t len)
{
    it->p   = data ? data : "";
    it->end = it->p + (data ? len : 0);
}

static bool line_iter_next(line_iter_t *it, const char **start, size_t *len)
{
    if (it->p >= it->end)
        return false;
    const char *begin = it->p;
    while (it->p < it->end && *it->p != '\n')
        it->p++;
    *start = begin;
    *len   = (size_t)(it->p - begin);
    if (it->p < it->end)
        it->p++; /* skip the newline */
    return true;
}

static void format_duration(uint32_t ms, char *buf, size_t size)
{
    uint32_t total = ms / 1000;
    ksnprintf(buf, size, "%02u:%02u:%02u", total / 3600, (total / 60) % 60,
              total % 60);
}

/* Human readable size, e.g. "1.4 KiB". */
static void format_size(size_t bytes, char *buf, size_t size)
{
    if (bytes < 1024)
        ksnprintf(buf, size, "%u B", (uint32_t)bytes);
    else if (bytes < 1024 * 1024)
        ksnprintf(buf, size, "%u.%u KiB", (uint32_t)(bytes / 1024),
                  (uint32_t)((bytes % 1024) * 10 / 1024));
    else
        ksnprintf(buf, size, "%u.%u MiB", (uint32_t)(bytes / (1024 * 1024)),
                  (uint32_t)((bytes % (1024 * 1024)) * 10 / (1024 * 1024)));
}

/* ------------------------------------------------------------------ */
/* Navigation                                                          */
/* ------------------------------------------------------------------ */

static int cmd_pwd(shell_t *sh, int argc, char **argv, stream_t *in,
                   stream_t *out, stream_t *err)
{
    /* Resolving first repairs the stored path if the directory was removed
     * from another window while this shell was standing in it. */
    shell_cwd_node(sh);
    stream_printf(out, "%s\n", shell_cwd(sh));
    return 0;
}

static int cmd_cd(shell_t *sh, int argc, char **argv, stream_t *in,
                  stream_t *out, stream_t *err)
{
    const char *target = argc > 1 ? argv[1] : "/home";
    char        resolved[FS_PATH_MAX];

    if (argc > 2) {
        stream_printf(err, "cd: too many arguments\n");
        return 1;
    }
    if (strcmp(target, "-") == 0)
        target = shell_previous_cwd(sh);

    path_join(shell_cwd(sh), target, resolved, sizeof(resolved));

    fs_node_t  *node = NULL;
    fs_result_t r    = fs_resolve(shell_cwd_node(sh), target, &node);
    if (r != FS_OK) {
        report(err, "cd", target, r);
        return 1;
    }
    if (node->type != FS_DIR) {
        report(err, "cd", target, FS_ERR_NOTDIR);
        return 1;
    }
    char actual[FS_PATH_MAX];
    fs_path(node, actual, sizeof(actual));
    shell_set_cwd(sh, actual);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Listing                                                             */
/* ------------------------------------------------------------------ */

static void ls_long_entry(stream_t *out, const fs_node_t *node)
{
    char stamp[16];
    char sizebuf[24];

    format_duration(node->modified, stamp, sizeof(stamp));
    if (node->type == FS_DIR)
        ksnprintf(sizebuf, sizeof(sizebuf), "%u items",
                  (uint32_t)fs_child_count(node));
    else
        ksnprintf(sizebuf, sizeof(sizebuf), "%u B", (uint32_t)node->size);

    stream_printf(out, "%c  %10s  %s  %s%s\n",
                  node->type == FS_DIR ? 'd' : '-', sizebuf, stamp, node->name,
                  node->type == FS_DIR ? "/" : "");
}

static void ls_columns(stream_t *out, const fs_node_t *dir)
{
    size_t widest = 0;
    for (fs_node_t *c = dir->children; c; c = c->next) {
        size_t len = strlen(c->name) + (c->type == FS_DIR ? 1 : 0);
        if (len > widest)
            widest = len;
    }
    if (widest == 0)
        return;

    size_t column = widest + 2;
    size_t per_line = 76 / column;
    if (per_line == 0)
        per_line = 1;

    size_t index = 0;
    for (fs_node_t *c = dir->children; c; c = c->next, index++) {
        char name[FS_NAME_MAX + 2];
        ksnprintf(name, sizeof(name), "%s%s", c->name,
                  c->type == FS_DIR ? "/" : "");
        if (index % per_line == per_line - 1 || !c->next)
            stream_printf(out, "%s\n", name);
        else
            stream_printf(out, "%-*s", (int)column, name);
    }
}

static int cmd_ls(shell_t *sh, int argc, char **argv, stream_t *in,
                  stream_t *out, stream_t *err)
{
    char flags[8];
    bool ok;
    int  first = parse_flags(argc, argv, "la1", flags, sizeof(flags), err, &ok);
    if (!ok)
        return 2;

    bool long_form = has_flag(flags, 'l');
    bool one_col   = has_flag(flags, '1');
    int  status    = 0;
    int  paths     = argc - first;

    if (paths <= 0) {
        static char *dot[] = { ".", NULL };
        argv               = dot;
        first              = 0;
        paths              = 1;
    }

    for (int i = 0; i < paths; i++) {
        const char *path = argv[first + i];
        fs_node_t  *node = NULL;
        fs_result_t r    = fs_resolve(shell_cwd_node(sh), path, &node);
        if (r != FS_OK) {
            report(err, "ls", path, r);
            status = 1;
            continue;
        }
        if (paths > 1)
            stream_printf(out, "%s%s:\n", i ? "\n" : "", path);

        if (node->type == FS_FILE) {
            if (long_form)
                ls_long_entry(out, node);
            else
                stream_printf(out, "%s\n", node->name);
            continue;
        }
        if (long_form) {
            stream_printf(out, "total %u\n",
                          (uint32_t)fs_child_count(node));
            for (fs_node_t *c = node->children; c; c = c->next)
                ls_long_entry(out, c);
        } else if (one_col) {
            for (fs_node_t *c = node->children; c; c = c->next)
                stream_printf(out, "%s%s\n", c->name,
                              c->type == FS_DIR ? "/" : "");
        } else {
            ls_columns(out, node);
        }
    }
    return status;
}

static void tree_walk(stream_t *out, const fs_node_t *dir, const char *prefix,
                      size_t *dirs, size_t *files)
{
    for (fs_node_t *c = dir->children; c; c = c->next) {
        bool last = c->next == NULL;
        stream_printf(out, "%s%s%s%s\n", prefix, last ? "`-- " : "|-- ",
                      c->name, c->type == FS_DIR ? "/" : "");
        if (c->type == FS_DIR) {
            (*dirs)++;
            char child_prefix[128];
            ksnprintf(child_prefix, sizeof(child_prefix), "%s%s", prefix,
                      last ? "    " : "|   ");
            tree_walk(out, c, child_prefix, dirs, files);
        } else {
            (*files)++;
        }
    }
}

static int cmd_tree(shell_t *sh, int argc, char **argv, stream_t *in,
                    stream_t *out, stream_t *err)
{
    const char *path = argc > 1 ? argv[1] : ".";
    fs_node_t  *node = NULL;

    fs_result_t r = fs_resolve(shell_cwd_node(sh), path, &node);
    if (r != FS_OK) {
        report(err, "tree", path, r);
        return 1;
    }
    if (node->type != FS_DIR) {
        report(err, "tree", path, FS_ERR_NOTDIR);
        return 1;
    }

    char full[FS_PATH_MAX];
    fs_path(node, full, sizeof(full));
    stream_printf(out, "%s\n", full);

    size_t dirs = 0, files = 0;
    tree_walk(out, node, "", &dirs, &files);
    stream_printf(out, "\n%u directories, %u files\n", (uint32_t)dirs,
                  (uint32_t)files);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Creation and removal                                                */
/* ------------------------------------------------------------------ */

static int cmd_mkdir(shell_t *sh, int argc, char **argv, stream_t *in,
                     stream_t *out, stream_t *err)
{
    char flags[8];
    bool ok;
    int  first = parse_flags(argc, argv, "p", flags, sizeof(flags), err, &ok);
    if (!ok)
        return 2;
    if (first >= argc) {
        stream_puts(err, "mkdir: missing operand\n");
        usage_error(err, "mkdir");
        return 1;
    }

    bool parents = has_flag(flags, 'p');
    int  status  = 0;
    for (int i = first; i < argc; i++) {
        fs_result_t r = parents
                            ? fs_mkdir_p(shell_cwd_node(sh), argv[i], NULL)
                            : fs_mkdir(shell_cwd_node(sh), argv[i], NULL);
        if (r != FS_OK) {
            report(err, "mkdir", argv[i], r);
            status = 1;
        }
    }
    return status;
}

static int cmd_touch(shell_t *sh, int argc, char **argv, stream_t *in,
                     stream_t *out, stream_t *err)
{
    if (argc < 2) {
        stream_puts(err, "touch: missing operand\n");
        usage_error(err, "touch");
        return 1;
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        fs_result_t r = fs_touch(shell_cwd_node(sh), argv[i], NULL);
        if (r != FS_OK) {
            report(err, "touch", argv[i], r);
            status = 1;
        }
    }
    return status;
}

static int cmd_rm(shell_t *sh, int argc, char **argv, stream_t *in,
                  stream_t *out, stream_t *err)
{
    char flags[8];
    bool ok;
    int  first = parse_flags(argc, argv, "rRf", flags, sizeof(flags), err, &ok);
    if (!ok)
        return 2;

    bool recursive = has_flag(flags, 'r') || has_flag(flags, 'R');
    bool force     = has_flag(flags, 'f');

    if (first >= argc) {
        if (force)
            return 0;
        stream_puts(err, "rm: missing operand\n");
        usage_error(err, "rm");
        return 1;
    }

    int status = 0;
    for (int i = first; i < argc; i++) {
        fs_node_t  *node = NULL;
        fs_result_t r    = fs_resolve(shell_cwd_node(sh), argv[i], &node);
        if (r != FS_OK) {
            if (!force) {
                report(err, "rm", argv[i], r);
                status = 1;
            }
            continue;
        }
        if (node->type == FS_DIR && !recursive) {
            stream_printf(err, "rm: %s: is a directory (use -r)\n", argv[i]);
            status = 1;
            continue;
        }
        /* Refuse to delete the directory the shell is standing in. */
        if (fs_is_ancestor(node, shell_cwd_node(sh))) {
            stream_printf(err,
                          "rm: %s: cannot remove the current directory\n",
                          argv[i]);
            status = 1;
            continue;
        }
        r = fs_remove_node(node, recursive);
        if (r != FS_OK) {
            report(err, "rm", argv[i], r);
            status = 1;
        }
    }
    return status;
}

/* ------------------------------------------------------------------ */
/* Copy and move                                                       */
/* ------------------------------------------------------------------ */

static int copy_or_move(shell_t *sh, int argc, char **argv, stream_t *err,
                        bool move)
{
    const char *name = move ? "mv" : "cp";
    char        flags[8];
    bool        ok;
    int first = parse_flags(argc, argv, move ? "f" : "rRf", flags,
                            sizeof(flags), err, &ok);
    if (!ok)
        return 2;

    bool recursive = has_flag(flags, 'r') || has_flag(flags, 'R');
    int  count     = argc - first;
    if (count < 2) {
        stream_printf(err, "%s: missing %s operand\n", name,
                      count == 1 ? "destination" : "file");
        usage_error(err, name);
        return 1;
    }

    const char *dest = argv[argc - 1];
    int         nsrc = count - 1;

    /* With more than one source the destination must be a directory. */
    fs_node_t *dest_node = NULL;
    bool dest_is_dir = fs_resolve(shell_cwd_node(sh), dest, &dest_node) == FS_OK &&
                       dest_node->type == FS_DIR;
    if (nsrc > 1 && !dest_is_dir) {
        stream_printf(err, "%s: target '%s' is not a directory\n", name, dest);
        return 1;
    }

    int status = 0;
    for (int i = 0; i < nsrc; i++) {
        const char *src = argv[first + i];
        fs_result_t r   = move ? fs_move(shell_cwd_node(sh), src, dest)
                               : fs_copy(shell_cwd_node(sh), src, dest,
                                         recursive);
        if (r != FS_OK) {
            if (!move && r == FS_ERR_ISDIR)
                stream_printf(err, "%s: %s: is a directory (use -r)\n", name,
                              src);
            else
                stream_printf(err, "%s: %s -> %s: %s\n", name, src, dest,
                              fs_strerror(r));
            status = 1;
        }
    }
    return status;
}

static int cmd_cp(shell_t *sh, int argc, char **argv, stream_t *in,
                  stream_t *out, stream_t *err)
{
    return copy_or_move(sh, argc, argv, err, false);
}

static int cmd_mv(shell_t *sh, int argc, char **argv, stream_t *in,
                  stream_t *out, stream_t *err)
{
    return copy_or_move(sh, argc, argv, err, true);
}

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */

static int cmd_echo(shell_t *sh, int argc, char **argv, stream_t *in,
                    stream_t *out, stream_t *err)
{
    int  first   = 1;
    bool newline = true;

    if (argc > 1 && strcmp(argv[1], "-n") == 0) {
        newline = false;
        first   = 2;
    }
    for (int i = first; i < argc; i++) {
        if (i > first)
            stream_putc(out, ' ');
        stream_puts(out, argv[i]);
    }
    if (newline)
        stream_putc(out, '\n');
    return 0;
}

static int cmd_cat(shell_t *sh, int argc, char **argv, stream_t *in,
                   stream_t *out, stream_t *err)
{
    if (argc < 2) {
        stream_write(out, in->data, in->size);
        return 0;
    }

    int status = 0;
    for (int i = 1; i < argc; i++) {
        fs_node_t  *node = NULL;
        fs_result_t r    = fs_resolve(shell_cwd_node(sh), argv[i], &node);
        if (r != FS_OK) {
            report(err, "cat", argv[i], r);
            status = 1;
            continue;
        }
        if (node->type != FS_FILE) {
            report(err, "cat", argv[i], FS_ERR_ISDIR);
            status = 1;
            continue;
        }
        stream_write(out, fs_data(node), fs_size(node));
        if (fs_size(node) && fs_data(node)[fs_size(node) - 1] != '\n')
            stream_putc(out, '\n');
    }
    return status;
}

static int grep_buffer(stream_t *out, const char *pattern, const char *data,
                       size_t len, const char *label, bool ignore_case,
                       bool invert, bool numbers, bool count_only,
                       size_t *matches)
{
    line_iter_t it;
    line_iter_init(&it, data, len);

    const char *start;
    size_t      line_len;
    size_t      lineno = 0;

    while (line_iter_next(&it, &start, &line_len)) {
        lineno++;
        char buf[512];
        size_t n = line_len < sizeof(buf) - 1 ? line_len : sizeof(buf) - 1;
        memcpy(buf, start, n);
        buf[n] = '\0';

        bool hit = regex_match(pattern, buf, ignore_case);
        if (hit == invert)
            continue;
        (*matches)++;
        if (count_only)
            continue;
        if (label)
            stream_printf(out, "%s:", label);
        if (numbers)
            stream_printf(out, "%u:", (uint32_t)lineno);
        stream_printf(out, "%s\n", buf);
    }
    return 0;
}

static int cmd_grep(shell_t *sh, int argc, char **argv, stream_t *in,
                    stream_t *out, stream_t *err)
{
    char flags[8];
    bool ok;
    int  first = parse_flags(argc, argv, "invc", flags, sizeof(flags), err, &ok);
    if (!ok)
        return 2;
    if (first >= argc) {
        stream_puts(err, "grep: missing search pattern\n");
        usage_error(err, "grep");
        return 2;
    }

    const char *pattern     = argv[first];
    bool        ignore_case = has_flag(flags, 'i');
    bool        invert      = has_flag(flags, 'v');
    bool        numbers     = has_flag(flags, 'n');
    bool        count_only  = has_flag(flags, 'c');
    size_t      matches     = 0;
    int         status      = 0;
    int         nfiles      = argc - first - 1;

    if (nfiles == 0) {
        grep_buffer(out, pattern, in->data, in->size, NULL, ignore_case,
                    invert, numbers, count_only, &matches);
    } else {
        for (int i = first + 1; i < argc; i++) {
            fs_node_t  *node = NULL;
            fs_result_t r = fs_resolve(shell_cwd_node(sh), argv[i], &node);
            if (r != FS_OK) {
                report(err, "grep", argv[i], r);
                status = 2;
                continue;
            }
            if (node->type != FS_FILE) {
                report(err, "grep", argv[i], FS_ERR_ISDIR);
                status = 2;
                continue;
            }
            grep_buffer(out, pattern, (const char *)fs_data(node),
                        fs_size(node), nfiles > 1 ? argv[i] : NULL,
                        ignore_case, invert, numbers, count_only, &matches);
        }
    }

    if (count_only)
        stream_printf(out, "%u\n", (uint32_t)matches);
    if (status)
        return status;
    return matches ? 0 : 1;
}

/* Collect the input for commands that accept "files or stdin". */
static bool gather_input(shell_t *sh, int argc, char **argv, int first,
                         stream_t *in, stream_t *err, const char *name,
                         stream_t *collected, int *status)
{
    stream_init(collected);
    if (first >= argc) {
        stream_write(collected, in->data, in->size);
        return true;
    }
    bool any = false;
    for (int i = first; i < argc; i++) {
        fs_node_t  *node = NULL;
        fs_result_t r    = fs_resolve(shell_cwd_node(sh), argv[i], &node);
        if (r != FS_OK) {
            report(err, name, argv[i], r);
            *status = 1;
            continue;
        }
        if (node->type != FS_FILE) {
            report(err, name, argv[i], FS_ERR_ISDIR);
            *status = 1;
            continue;
        }
        stream_write(collected, fs_data(node), fs_size(node));
        any = true;
    }
    return any;
}

static int cmd_wc(shell_t *sh, int argc, char **argv, stream_t *in,
                  stream_t *out, stream_t *err)
{
    char flags[8];
    bool ok;
    int  first = parse_flags(argc, argv, "lwc", flags, sizeof(flags), err, &ok);
    if (!ok)
        return 2;

    int      status = 0;
    stream_t data;
    gather_input(sh, argc, argv, first, in, err, "wc", &data, &status);

    size_t lines = 0, words = 0, bytes = data.size;
    bool   in_word = false;
    for (size_t i = 0; i < data.size; i++) {
        char c = data.data[i];
        if (c == '\n')
            lines++;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            words++;
        }
    }
    if (data.size && data.data[data.size - 1] != '\n')
        lines++;

    bool show_all = flags[0] == '\0';
    if (show_all || has_flag(flags, 'l'))
        stream_printf(out, "%8u", (uint32_t)lines);
    if (show_all || has_flag(flags, 'w'))
        stream_printf(out, "%8u", (uint32_t)words);
    if (show_all || has_flag(flags, 'c'))
        stream_printf(out, "%8u", (uint32_t)bytes);
    stream_putc(out, '\n');

    stream_free(&data);
    return status;
}

static int head_tail(shell_t *sh, int argc, char **argv, stream_t *in,
                     stream_t *out, stream_t *err, bool head)
{
    const char *name  = head ? "head" : "tail";
    int         count = 10;
    int         first = 1;

    if (argc > 2 && strcmp(argv[1], "-n") == 0) {
        count = katoi(argv[2]);
        first = 3;
        if (count <= 0)
            count = 10;
    }

    int      status = 0;
    stream_t data;
    gather_input(sh, argc, argv, first, in, err, name, &data, &status);

    /* Count the lines first so tail can skip the leading ones. */
    size_t total = 0;
    {
        line_iter_t it;
        line_iter_init(&it, data.data, data.size);
        const char *s;
        size_t      l;
        while (line_iter_next(&it, &s, &l))
            total++;
    }

    line_iter_t it;
    line_iter_init(&it, data.data, data.size);
    const char *s;
    size_t      l;
    size_t      index = 0;
    size_t      skip  = head ? 0 : (total > (size_t)count ? total - count : 0);

    while (line_iter_next(&it, &s, &l)) {
        if (index++ < skip)
            continue;
        if (head && index > (size_t)count)
            break;
        stream_write(out, s, l);
        stream_putc(out, '\n');
    }

    stream_free(&data);
    return status;
}

static int cmd_head(shell_t *sh, int argc, char **argv, stream_t *in,
                    stream_t *out, stream_t *err)
{
    return head_tail(sh, argc, argv, in, out, err, true);
}

static int cmd_tail(shell_t *sh, int argc, char **argv, stream_t *in,
                    stream_t *out, stream_t *err)
{
    return head_tail(sh, argc, argv, in, out, err, false);
}

static int cmd_sort(shell_t *sh, int argc, char **argv, stream_t *in,
                    stream_t *out, stream_t *err)
{
    char flags[8];
    bool ok;
    int  first = parse_flags(argc, argv, "r", flags, sizeof(flags), err, &ok);
    if (!ok)
        return 2;

    int      status = 0;
    stream_t data;
    gather_input(sh, argc, argv, first, in, err, "sort", &data, &status);

    /* Split into lines. */
    size_t capacity = 32, count = 0;
    char **lines    = kmalloc(capacity * sizeof(char *));
    if (!lines) {
        stream_free(&data);
        return 1;
    }

    line_iter_t it;
    line_iter_init(&it, data.data, data.size);
    const char *s;
    size_t      l;
    while (line_iter_next(&it, &s, &l)) {
        if (count == capacity) {
            capacity *= 2;
            char **grown = krealloc(lines, capacity * sizeof(char *));
            if (!grown)
                break;
            lines = grown;
        }
        lines[count++] = kstrndup(s, l);
    }

    /* Insertion sort: the inputs here are small and it keeps the code
     * obvious. */
    for (size_t i = 1; i < count; i++) {
        char  *key = lines[i];
        size_t j   = i;
        while (j > 0 && strcmp(lines[j - 1], key) > 0) {
            lines[j] = lines[j - 1];
            j--;
        }
        lines[j] = key;
    }

    bool reverse = has_flag(flags, 'r');
    for (size_t i = 0; i < count; i++) {
        const char *line = lines[reverse ? count - 1 - i : i];
        stream_printf(out, "%s\n", line);
    }
    for (size_t i = 0; i < count; i++)
        kfree(lines[i]);
    kfree(lines);
    stream_free(&data);
    return status;
}

/* ------------------------------------------------------------------ */
/* Information                                                         */
/* ------------------------------------------------------------------ */

static int cmd_stat(shell_t *sh, int argc, char **argv, stream_t *in,
                    stream_t *out, stream_t *err)
{
    if (argc < 2) {
        stream_puts(err, "stat: missing operand\n");
        return 1;
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        fs_node_t  *node = NULL;
        fs_result_t r    = fs_resolve(shell_cwd_node(sh), argv[i], &node);
        if (r != FS_OK) {
            report(err, "stat", argv[i], r);
            status = 1;
            continue;
        }
        char full[FS_PATH_MAX], stamp[16], sizebuf[24];
        fs_path(node, full, sizeof(full));
        format_duration(node->modified, stamp, sizeof(stamp));
        format_size(fs_size_recursive(node), sizebuf, sizeof(sizebuf));

        stream_printf(out, "  path: %s\n", full);
        stream_printf(out, "  type: %s\n",
                      node->type == FS_DIR ? "directory" : "regular file");
        stream_printf(out, "  size: %s\n", sizebuf);
        if (node->type == FS_DIR)
            stream_printf(out, " items: %u\n",
                          (uint32_t)fs_child_count(node));
        stream_printf(out, "  time: %s (since boot)\n", stamp);
    }
    return status;
}

static int cmd_date(shell_t *sh, int argc, char **argv, stream_t *in,
                    stream_t *out, stream_t *err)
{
    sys_info_t info;
    sys_info_get(&info);
    stream_printf(out, "%s\n", info.datetime);
    return 0;
}

static int cmd_uptime(shell_t *sh, int argc, char **argv, stream_t *in,
                      stream_t *out, stream_t *err)
{
    sys_info_t info;
    sys_info_get(&info);
    char buf[16];
    format_duration(info.uptime_ms, buf, sizeof(buf));
    stream_printf(out, "up %s\n", buf);
    return 0;
}

static int cmd_uname(shell_t *sh, int argc, char **argv, stream_t *in,
                     stream_t *out, stream_t *err)
{
    sys_info_t info;
    sys_info_get(&info);
    if (argc > 1 && strcmp(argv[1], "-a") == 0)
        stream_printf(out, "%s %s %s\n", TINOS_NAME " " TINOS_VERSION,
                      info.platform, info.cpu);
    else
        stream_printf(out, "%s\n", TINOS_NAME);
    return 0;
}

static int cmd_free(shell_t *sh, int argc, char **argv, stream_t *in,
                    stream_t *out, stream_t *err)
{
    sys_info_t info;
    sys_info_get(&info);

    char total[24], freebuf[24], heap_total[24], heap_used[24], fsbuf[24];
    format_size((size_t)info.mem_total, total, sizeof(total));
    format_size((size_t)info.mem_free, freebuf, sizeof(freebuf));
    format_size(info.heap_total, heap_total, sizeof(heap_total));
    format_size(info.heap_used, heap_used, sizeof(heap_used));
    format_size(fs_total_bytes(), fsbuf, sizeof(fsbuf));

    stream_printf(out, "physical memory : %s total, %s free\n", total, freebuf);
    stream_printf(out, "kernel heap     : %s total, %s in use\n", heap_total,
                  heap_used);
    stream_printf(out, "filesystem      : %s in %u nodes\n", fsbuf,
                  (uint32_t)fs_node_count());
    return 0;
}

static int cmd_help(shell_t *sh, int argc, char **argv, stream_t *in,
                    stream_t *out, stream_t *err)
{
    size_t                  count;
    const sh_command_def_t *cmds = sh_commands(&count);

    if (argc > 1) {
        const sh_command_def_t *def = sh_find_command(argv[1]);
        if (!def) {
            stream_printf(err, "help: no such command: %s\n", argv[1]);
            return 1;
        }
        stream_printf(out, "%s - %s\n", def->name, def->summary);
        stream_printf(out, "usage: %s\n", def->usage);
        return 0;
    }

    stream_puts(out, "TinOS shell - built-in commands\n\n");
    for (size_t i = 0; i < count; i++)
        stream_printf(out, "  %-10s %s\n", cmds[i].name, cmds[i].summary);
    stream_puts(out,
                "\nOperators:\n"
                "  cmd > file     write output to a file (>> appends)\n"
                "  cmd < file     read input from a file\n"
                "  a | b          pipe a's output into b\n"
                "  a & b / a ; b  run a, then b (sequential)\n"
                "  a && b         run b only if a succeeded\n"
                "  a || b         run b only if a failed\n"
                "\nQuoting: 'single' and \"double\" quotes and \\ escapes are"
                " supported.\n"
                "Type 'help <command>' for details on one command.\n");
    return 0;
}

static int cmd_history(shell_t *sh, int argc, char **argv, stream_t *in,
                       stream_t *out, stream_t *err)
{
    size_t count = shell_history_count(sh);
    for (size_t i = 0; i < count; i++)
        stream_printf(out, "%4u  %s\n", (uint32_t)(i + 1),
                      shell_history_at(sh, i));
    return 0;
}

/* ------------------------------------------------------------------ */
/* Commands that talk to the desktop                                   */
/* ------------------------------------------------------------------ */

static int cmd_clear(shell_t *sh, int argc, char **argv, stream_t *in,
                     stream_t *out, stream_t *err)
{
    shell_request(sh, SH_REQ_CLEAR, NULL);
    return 0;
}

static int cmd_exit(shell_t *sh, int argc, char **argv, stream_t *in,
                    stream_t *out, stream_t *err)
{
    shell_request(sh, SH_REQ_EXIT, NULL);
    return 0;
}

static int cmd_explorer(shell_t *sh, int argc, char **argv, stream_t *in,
                        stream_t *out, stream_t *err)
{
    char path[FS_PATH_MAX];
    path_join(shell_cwd(sh), argc > 1 ? argv[1] : ".", path, sizeof(path));

    fs_node_t  *node = NULL;
    fs_result_t r    = fs_resolve(fs_root(), path, &node);
    if (r != FS_OK) {
        report(err, "explorer", path, r);
        return 1;
    }
    if (node->type != FS_DIR) {
        report(err, "explorer", path, FS_ERR_NOTDIR);
        return 1;
    }
    shell_request(sh, SH_REQ_EXPLORER, path);
    return 0;
}

static int cmd_edit(shell_t *sh, int argc, char **argv, stream_t *in,
                    stream_t *out, stream_t *err)
{
    if (argc < 2) {
        stream_puts(err, "edit: missing file name\n");
        usage_error(err, "edit");
        return 1;
    }
    char path[FS_PATH_MAX];
    path_join(shell_cwd(sh), argv[1], path, sizeof(path));

    fs_node_t  *node = NULL;
    fs_result_t r    = fs_resolve(fs_root(), path, &node);
    if (r == FS_ERR_NOENT) {
        r = fs_create(fs_root(), path, &node);
        if (r != FS_OK) {
            report(err, "edit", path, r);
            return 1;
        }
    } else if (r != FS_OK) {
        report(err, "edit", path, r);
        return 1;
    }
    if (node->type != FS_FILE) {
        report(err, "edit", path, FS_ERR_ISDIR);
        return 1;
    }
    shell_request(sh, SH_REQ_EDITOR, path);
    return 0;
}

static int cmd_sysinfo(shell_t *sh, int argc, char **argv, stream_t *in,
                       stream_t *out, stream_t *err)
{
    sys_info_t info;
    sys_info_get(&info);

    char uptime[16], total[24], freebuf[24];
    format_duration(info.uptime_ms, uptime, sizeof(uptime));
    format_size((size_t)info.mem_total, total, sizeof(total));
    format_size((size_t)info.mem_free, freebuf, sizeof(freebuf));

    stream_printf(out, "%s %s\n", TINOS_NAME, TINOS_VERSION);
    stream_printf(out, "  platform : %s\n", info.platform);
    stream_printf(out, "  cpu      : %s\n", info.cpu);
    stream_printf(out, "  memory   : %s (%s free)\n", total, freebuf);
    stream_printf(out, "  display  : %dx%d 32bpp\n", info.screen_width,
                  info.screen_height);
    stream_printf(out, "  uptime   : %s\n", uptime);
    stream_printf(out, "  time     : %s\n", info.datetime);
    stream_printf(out, "  files    : %u nodes\n", (uint32_t)fs_node_count());
    if (argc > 1 && strcmp(argv[1], "-w") == 0)
        shell_request(sh, SH_REQ_SYSINFO, NULL);
    return 0;
}

static int cmd_about(shell_t *sh, int argc, char **argv, stream_t *in,
                     stream_t *out, stream_t *err)
{
    shell_request(sh, SH_REQ_ABOUT, NULL);
    stream_printf(out, "%s %s - a small graphical operating system\n",
                  TINOS_NAME, TINOS_VERSION);
    return 0;
}

/* Deliberate fault injection, so the exception handler and the diagnostic
 * panic screen can be demonstrated (and tested) on purpose.  An argument is
 * required, because this really does stop the machine. */
static int cmd_crash(shell_t *sh, int argc, char **argv, stream_t *in,
                     stream_t *out, stream_t *err)
{
    if (argc < 2) {
        stream_puts(out,
                    "crash: halts TinOS on purpose to show the kernel's\n"
                    "       diagnostic panic screen. Pick a fault:\n"
                    "         crash divide   division by zero (exception 0)\n"
                    "         crash opcode   invalid instruction (exception 6)\n"
                    "         crash gpf      bad segment selector (exception 13)\n"
                    "         crash panic    an explicit kernel panic\n");
        return 1;
    }

#if defined(__i386__)
    if (strcmp(argv[1], "divide") == 0) {
        /* Written in assembly: a C division by zero is undefined behaviour
         * that the optimiser is free to delete. */
        __asm__ volatile("xorl %%edx, %%edx\n\t"
                         "xorl %%ecx, %%ecx\n\t"
                         "movl $1, %%eax\n\t"
                         "divl %%ecx"
                         :
                         :
                         : "eax", "ecx", "edx", "cc");
    } else if (strcmp(argv[1], "opcode") == 0) {
        __asm__ volatile("ud2");
    } else if (strcmp(argv[1], "gpf") == 0) {
        /* TinOS runs without paging, so a wild pointer write does not fault;
         * loading an invalid selector reliably does. */
        __asm__ volatile("movl $0x1234, %%eax\n\t"
                         "movl %%eax, %%ds"
                         :
                         :
                         : "eax");
    } else
#endif
        if (strcmp(argv[1], "panic") == 0) {
        panic("crash command invoked by the user from the terminal");
    } else {
        stream_printf(err, "crash: unknown fault '%s'\n", argv[1]);
        return 1;
    }
    stream_puts(out, "crash: the fault did not trigger\n");
    return 1;
}

static int cmd_reboot(shell_t *sh, int argc, char **argv, stream_t *in,
                      stream_t *out, stream_t *err)
{
    shell_request(sh, SH_REQ_REBOOT, NULL);
    return 0;
}

static int cmd_shutdown(shell_t *sh, int argc, char **argv, stream_t *in,
                        stream_t *out, stream_t *err)
{
    shell_request(sh, SH_REQ_SHUTDOWN, NULL);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Table                                                               */
/* ------------------------------------------------------------------ */

static int cmd_ping(shell_t *sh, int argc, char **argv, stream_t *in,
                    stream_t *out, stream_t *err)
{
    if (argc != 2) { stream_puts(err, "usage: ping <address|hostname>\n"); return 2; }
    if (!net_available() || !net_config()->configured) { stream_puts(err, "ping: network is not configured\n"); return 1; }
    uint32_t id = net_async_submit(NET_JOB_PING, argv[1], NULL);
    if (!id) { stream_puts(err, "ping: network worker queue is full\n"); return 1; }
    stream_printf(out, "ping: request %u queued\n", id);
    return 0;
}

static int cmd_nslookup(shell_t *sh, int argc, char **argv, stream_t *in,
                        stream_t *out, stream_t *err)
{
    if (argc != 2) { stream_puts(err, "usage: nslookup <hostname>\n"); return 2; }
    if (!net_available() || !net_config()->configured) { stream_puts(err, "nslookup: network is not configured\n"); return 1; }
    uint32_t id = net_async_submit(NET_JOB_DNS, argv[1], NULL);
    if (!id) { stream_puts(err, "nslookup: network worker queue is full\n"); return 1; }
    stream_printf(out, "nslookup: request %u queued\n", id);
    return 0;
}

static int cmd_ifconfig(shell_t *sh, int argc, char **argv, stream_t *in,
                        stream_t *out, stream_t *err)
{
    if (argc != 1) { stream_puts(err, "usage: ifconfig\n"); return 2; }
    if (!net_available()) { stream_puts(out, "network: unavailable\n"); return 1; }
    const net_config_t *c = net_config();
    char ip[20], mask[20], gateway[20], dns[20];
    net_format_address(c->address, ip, sizeof(ip)); net_format_address(c->netmask, mask, sizeof(mask));
    net_format_address(c->gateway, gateway, sizeof(gateway)); net_format_address(c->dns, dns, sizeof(dns));
    stream_printf(out, "eth0: %s\n  ip %s\n  mask %s\n  gateway %s\n  dns %s\n", c->configured ? "up" : "down", ip, mask, gateway, dns);
    return c->configured ? 0 : 1;
}

static int cmd_httpget(shell_t *sh, int argc, char **argv, stream_t *in,
                       stream_t *out, stream_t *err)
{
    if (argc < 2 || argc > 3) {
        stream_puts(err, "usage: httpget <host> [path]\n");
        return 2;
    }
    if (!net_available() || !net_config()->configured) {
        stream_puts(err, "httpget: network is not configured\n");
        return 1;
    }
    uint32_t id = net_async_submit(NET_JOB_HTTP, argv[1],
                                   argc == 3 ? argv[2] : "/");
    if (!id) { stream_puts(err, "httpget: network worker queue is full\n"); return 1; }
    stream_printf(out, "httpget: request %u queued\n", id);
    return 0;
}

static const sh_command_def_t commands[] = {
    { "about",    "about",
      "show the About window",                       cmd_about },
    { "cat",      "cat [file...]",
      "print file contents (or the piped input)",    cmd_cat },
    { "cd",       "cd [directory|-]",
      "change the working directory",                cmd_cd },
    { "clear",    "clear",
      "clear the terminal",                          cmd_clear },
    { "cp",       "cp [-r] source... destination",
      "copy files and directories",                  cmd_cp },
    { "crash",    "crash <divide|opcode|gpf|panic>",
      "trigger a fault to show the panic screen",    cmd_crash },
    { "date",     "date",
      "show the current date and time",              cmd_date },
    { "echo",     "echo [-n] text...",
      "write text to the output",                    cmd_echo },
    { "edit",     "edit <file>",
      "open a file in the text editor",              cmd_edit },
    { "exit",     "exit",
      "close this terminal window",                  cmd_exit },
    { "explorer", "explorer [directory]",
      "open a File Explorer window",                 cmd_explorer },
    { "free",     "free",
      "show memory and filesystem usage",            cmd_free },
    { "grep",     "grep [-invc] pattern [file...]",
      "search for text in files or piped input",     cmd_grep },
    { "head",     "head [-n count] [file...]",
      "show the first lines of the input",           cmd_head },
    { "httpget",  "httpget <host> [path]",
      "fetch a simple HTTP response",                cmd_httpget },
    { "ifconfig", "ifconfig",
      "show network interface configuration",       cmd_ifconfig },
    { "help",     "help [command]",
      "list the built-in commands",                  cmd_help },
    { "history",  "history",
      "list the commands used this session",         cmd_history },
    { "ls",       "ls [-la1] [path...]",
      "list directory contents",                     cmd_ls },
    { "mkdir",    "mkdir [-p] directory...",
      "create directories",                          cmd_mkdir },
    { "mv",       "mv source... destination",
      "move or rename files and directories",        cmd_mv },
    { "nslookup", "nslookup <hostname>",
      "resolve a DNS A record",                     cmd_nslookup },
    { "ping",     "ping <address|hostname>",
      "send an ICMP echo request",                   cmd_ping },
    { "pwd",      "pwd",
      "print the working directory",                 cmd_pwd },
    { "reboot",   "reboot",
      "restart the machine",                         cmd_reboot },
    { "rm",       "rm [-rf] path...",
      "remove files and directories",                cmd_rm },
    { "shutdown", "shutdown",
      "power off the machine",                       cmd_shutdown },
    { "sort",     "sort [-r] [file...]",
      "sort lines of text",                          cmd_sort },
    { "stat",     "stat path...",
      "show details about a file or directory",      cmd_stat },
    { "sysinfo",  "sysinfo [-w]",
      "show system information",                     cmd_sysinfo },
    { "tail",     "tail [-n count] [file...]",
      "show the last lines of the input",            cmd_tail },
    { "touch",    "touch file...",
      "create files or update their timestamp",      cmd_touch },
    { "tree",     "tree [directory]",
      "show a directory tree",                       cmd_tree },
    { "uname",    "uname [-a]",
      "show the system name",                        cmd_uname },
    { "uptime",   "uptime",
      "show how long the system has been running",   cmd_uptime },
    { "wc",       "wc [-lwc] [file...]",
      "count lines, words and bytes",                cmd_wc },
};

const sh_command_def_t *sh_commands(size_t *count)
{
    *count = ARRAY_SIZE(commands);
    return commands;
}

const sh_command_def_t *sh_find_command(const char *name)
{
    for (size_t i = 0; i < ARRAY_SIZE(commands); i++)
        if (strcmp(commands[i].name, name) == 0)
            return &commands[i];
    return NULL;
}
