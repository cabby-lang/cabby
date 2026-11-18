/* arena.h — bump allocator used for tokens' decoded strings and all AST nodes.
 * Everything allocated from an arena lives until arena_free(); the front end
 * never frees individual nodes. Allocations are zero-initialized. */
#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

typedef struct ArenaBlock ArenaBlock;

typedef struct Arena {
	ArenaBlock *head;
} Arena;

void  arena_init(Arena *a);
void *arena_alloc(Arena *a, size_t size);          /* zeroed, 16-byte aligned */
char *arena_strndup(Arena *a, const char *s, size_t n);
void  arena_free(Arena *a);

#endif
