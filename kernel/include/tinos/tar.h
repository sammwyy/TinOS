/* Minimal ustar reader: unpacks an in-memory tar archive into the ramfs. */
#ifndef TINOS_TAR_H
#define TINOS_TAR_H

#include <tinos/types.h>

/* Mounts regular files from the ustar archive at [image, image+size)
 * under dest_dir (created if it does not exist yet). The archive memory
 * must remain valid while the files exist. Returns the number of files. */
size_t tar_extract(const void *image, size_t size, const char *dest_dir);

#endif /* TINOS_TAR_H */
