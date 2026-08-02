/* Path string manipulation shared by the shell, the terminal prompt and the
 * file explorer.  Purely textual: nothing here touches the node tree. */

#include <tinos/fs.h>
#include <tinos/path.h>
#include <tinos/string.h>

void path_normalize(const char *path, char *out, size_t size)
{
    if (size == 0)
        return;

    /* Stack of component offsets so ".." can pop the previous one. */
    size_t starts[64];
    int    depth = 0;
    size_t pos   = 0;

    out[pos++] = '/';

    const char *p = path;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;

        const char *start = p;
        while (*p && *p != '/')
            p++;
        size_t len = (size_t)(p - start);

        if (len == 1 && start[0] == '.')
            continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (depth > 0)
                pos = starts[--depth];
            continue;
        }
        if (pos + len + 2 > size)
            break;
        if (depth < 64)
            starts[depth++] = pos;
        if (pos > 1)
            out[pos++] = '/';
        memcpy(out + pos, start, len);
        pos += len;
    }

    if (pos > 1 && out[pos - 1] == '/')
        pos--;
    out[pos] = '\0';
}

void path_join(const char *base, const char *rel, char *out, size_t size)
{
    char joined[FS_PATH_MAX * 2];

    if (!rel || rel[0] == '\0') {
        path_normalize(base, out, size);
        return;
    }
    if (rel[0] == '/') {
        path_normalize(rel, out, size);
        return;
    }

    size_t blen = strlcpy(joined, base && base[0] ? base : "/", sizeof(joined));
    if (blen >= sizeof(joined))
        blen = sizeof(joined) - 1;
    if (blen == 0 || joined[blen - 1] != '/') {
        if (blen + 1 < sizeof(joined))
            joined[blen++] = '/';
    }
    joined[blen] = '\0';
    strlcpy(joined + blen, rel, sizeof(joined) - blen);
    path_normalize(joined, out, size);
}

const char *path_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (!slash)
        return path;
    if (slash[1] == '\0')
        return slash == path ? "/" : slash;
    return slash + 1;
}

void path_dirname(const char *path, char *out, size_t size)
{
    char        normalized[FS_PATH_MAX];
    path_normalize(path, normalized, sizeof(normalized));

    char *slash = strrchr(normalized, '/');
    if (!slash || slash == normalized) {
        strlcpy(out, "/", size);
        return;
    }
    *slash = '\0';
    strlcpy(out, normalized, size);
}

bool path_has_suffix(const char *name, const char *suffix)
{
    size_t nlen = strlen(name);
    size_t slen = strlen(suffix);
    if (slen > nlen)
        return false;
    return kstrcasecmp(name + nlen - slen, suffix) == 0;
}
