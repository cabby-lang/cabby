#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

/* Reads a whole file into a malloc'd, NUL-terminated buffer.
 * Returns NULL (after printing why) on failure or if the file has NUL bytes. */
char *read_file(const char *path, size_t *len_out);

#endif
