/* Textual path helpers (no filesystem access). */
#ifndef TINOS_PATH_H
#define TINOS_PATH_H

#include <tinos/types.h>

/* Collapse ".", ".." and duplicate separators in an absolute path. */
void path_normalize(const char *path, char *out, size_t size);

/* Join `rel` onto `base` (absolute) and normalise the result. */
void path_join(const char *base, const char *rel, char *out, size_t size);

/* Final component of a path ("/a/b.txt" -> "b.txt", "/" -> "/"). */
const char *path_basename(const char *path);

/* Everything before the final component ("/a/b.txt" -> "/a"). */
void path_dirname(const char *path, char *out, size_t size);

/* True when the name ends with the given (case-insensitive) suffix. */
bool path_has_suffix(const char *name, const char *suffix);

#endif /* TINOS_PATH_H */
