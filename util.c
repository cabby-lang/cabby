#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *read_file(const char *path, size_t *len_out)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "error: can't open '%s': %s\n", path, strerror(errno));
		return NULL;
	}
	size_t cap = 4096, len = 0;
	char *buf = malloc(cap);
	for (;;) {
		if (!buf) {
			fprintf(stderr, "error: out of memory reading '%s'\n", path);
			fclose(f);
			return NULL;
		}
		len += fread(buf + len, 1, cap - len - 1, f);
		if (len < cap - 1)
			break;
		cap *= 2;
		char *nb = realloc(buf, cap);
		if (!nb)
			free(buf);
		buf = nb;
	}
	int err = ferror(f);
	fclose(f);
	if (err) {
		fprintf(stderr, "error: can't read '%s'\n", path);
		free(buf);
		return NULL;
	}
	buf[len] = '\0';
	if (memchr(buf, '\0', len)) {
		fprintf(stderr, "error: '%s' contains NUL bytes; is it a binary file?\n", path);
		free(buf);
		return NULL;
	}
	if (len_out)
		*len_out = len;
	return buf;
}
