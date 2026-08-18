/* Minimal ustar reader: unpacks an in-memory tar archive into the ramfs. */
#ifndef TINOS_TAR_H
#define TINOS_TAR_H

#include <tinos/types.h>

/* Extracts every regular file in the ustar archive at [image, image+size)
 * under dest_dir (created if it does not exist yet). Returns the number of
 * files written. */
size_t tar_extract(const void *image, size_t size, const char *dest_dir);

#endif /* TINOS_TAR_H */
