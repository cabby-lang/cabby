#include "arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARENA_BLOCK_SIZE (64 * 1024)
#define ARENA_ALIGN 16

struct ArenaBlock {
	ArenaBlock *next;
	size_t used;
	size_t cap;
	_Alignas(ARENA_ALIGN) unsigned char data[];
};

void arena_init(Arena *a) { a->head = NULL; }

static ArenaBlock *new_block(size_t min)
{
	size_t cap = min > ARENA_BLOCK_SIZE ? min : ARENA_BLOCK_SIZE;
	ArenaBlock *b = malloc(sizeof *b + cap);
	if (!b) {
		fprintf(stderr, "fatal: out of memory\n");
		exit(1);
	}
	b->next = NULL;
	b->used = 0;
	b->cap = cap;
	return b;
}

void *arena_alloc(Arena *a, size_t size)
{
	size = (size + ARENA_ALIGN - 1) & ~(size_t)(ARENA_ALIGN - 1);
	if (size == 0)
		size = ARENA_ALIGN;
	if (!a->head || a->head->cap - a->head->used < size) {
		ArenaBlock *b = new_block(size);
		b->next = a->head;
		a->head = b;
	}
	void *p = a->head->data + a->head->used;
	a->head->used += size;
	memset(p, 0, size);
	return p;
}

char *arena_strndup(Arena *a, const char *s, size_t n)
{
	char *p = arena_alloc(a, n + 1);
	memcpy(p, s, n);
	p[n] = '\0';
	return p;
}

void arena_free(Arena *a)
{
	ArenaBlock *b = a->head;
	while (b) {
		ArenaBlock *next = b->next;
		free(b);
		b = next;
	}
	a->head = NULL;
}
