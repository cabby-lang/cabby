#define _GNU_SOURCE /* RTLD_DEFAULT */
#include "interpreter.h"

#include <dlfcn.h>
#include <inttypes.h>
#include <ffi.h>
#include <math.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "runtime/rt.h"
#ifdef __wasi__
/* (WebAssembly: C functions are found among wasm/calls.c's wrappers) */
#define dlsym(h, name) ((void *)cby_wasm_find(name))
#endif

/* ============================================================== values */

typedef enum {
	V_UNINIT = 0,   /* empty cell: moved-from or dropped */
	V_VOID,
	V_INT,          /* sign- or zero-extended according to its static type */
	V_FLOAT,        /* a float value is kept rounded to single precision */
	V_BOOL,
	V_STR,
	V_REF,          /* also slices: a reference to an array cell */
	V_ARRAY,
	V_OBJECT,       /* objects and structs */
	V_UNION,        /* raw bytes */
	V_HANDLER,      /* a window's method: object + function */
	V_LIST,         /* a growable list */
	V_LOOKUP,       /* values found by key */
	V_FILE,         /* an open file (rt_file *), or NULL */
	V_ADDRESS,      /* use addresses: an AddrVal *, or NULL for addresses.none */
} VKind;

typedef struct Str {
	int64_t len;
	int64_t cap;      /* room for text after the header, counting the NUL */
	bool is_static;   /* literal: never freed */
	char *data;       /* NUL-terminated for C */
} Str;

typedef struct HandlerVal {
	void *obj;        /* the window: an Object * */
	void *fn;         /* its method: a FnDecl * */
} HandlerVal;

typedef struct Array Array;

/* An address (use addresses). Cabby's own values aren't laid out as C
 * lays them out, so an address names a cell, or an element of an array,
 * which also lets the interpreter check it; memory from C is raw bytes. */
typedef struct AddrVal {
	struct Value *cell;      /* a single value */
	Array *arr;              /* or an element of this array */
	int64_t idx;             /* steps along from cell (only 0 can be used), or the element */
	unsigned char *raw;      /* or memory outside Cabby: raw + idx * the value's size */
	const struct Type *elem; /* cell or arr: the kind of value it was made for */
	struct List *list;       /* or an item of this list (idx) */
	int watch;               /* cell or arr: in g_watches, to know when the value is gone; -1 none */
} AddrVal;

typedef struct List List;
typedef struct Lookup Lookup;
typedef struct Object Object;
typedef struct Union Union;

typedef struct Value {
	VKind kind;
	union {
		int64_t i;
		double f;
		bool b;
		Str *s;
		struct Value *ref;
		Array *arr;
		Object *obj;
		Union *un;
		HandlerVal *hv;
		List *list;
		Lookup *lk;
		rt_file *file;
		AddrVal *ad;
	} as;
} Value;

struct List {
	int64_t len, cap;
	Value *items;     /* cap cells; the first len are in use */
};

/* Entries in the order they were added; removing one leaves it dead until
 * the next add that needs room tidies them (see runtime/rt.h). */
struct Lookup {
	int64_t n, len, cap, icap;   /* positions used (live or dead), live, room, index size */
	Value *keys, *vals;
	uint8_t *dead;
	int32_t *index;   /* position + 1; 0 empty; -1 removed */
};

/* An array of numbers (bool, char, int, long, double...) keeps them as C
 * does, each in its own size, rather than as Values: a million bools take
 * a megabyte. `raw` is then their type, and the bytes start at elems.
 * Lending an element, or taking an address of one, spreads the array out
 * into Values first (array_spread), since those keep a Value's place. */
struct Array {
	int64_t len;
	const Type *raw;   /* numbers as bytes: their type; else NULL */
	int64_t es;        /* and each one's size */
	Value elems[];
};
#define ARR_BYTES(a) ((unsigned char *)(a)->elems)

struct Object {
	ObjectInfo *cls;   /* exact class */
	uint64_t life;     /* its place in the runtime's table of linked objects (rt.c), 0 for none */
	Value as_value;    /* (itself, as a value: where a link's use of it is, once linked) */
	Value fields[];    /* in cls->fields slot order */
};

struct Union {
	ObjectInfo *cls;
	uint64_t size;
	unsigned char bytes[];
};

typedef enum { EX_NORMAL, EX_BREAK, EX_CONTINUE, EX_RETURN, EX_GOTO } Exec;

typedef struct Frame {
	Value *cells;
	int ncells;
	Value ret;
	struct Frame *caller;
	FnDecl *fn;              /* the function, for messages */
} Frame;

#define MAX_DEPTH 100000
#define INTERP_STACK_SIZE ((size_t)1 << 30)

/* A temporary cell. Union fields are accessed through a proxy cell whose
 * value is written back into the union's bytes when the statement ends. */
typedef struct Temp {
	Value cell;
	unsigned char *union_bytes;   /* proxy: where to write back, or NULL */
	Type *proxy_type;
	unsigned char *proxy_was;     /* proxy: the bytes when it was made */
	unsigned char was_here[8];    /* (where they are, when that's enough) */
	struct Temp *next_free;       /* in Interp.free_temps once released */
} Temp;

/* Function calls take their cells from a stack made of chunks that never
 * move (references point into callers' cells), rather than from malloc. */
typedef struct CellChunk {
	struct CellChunk *prev;
	int64_t cap, used;
	Value cells[];
} CellChunk;

typedef struct Interp {
	Sema *S;
	Frame *frame;
	CellChunk *chunk;             /* the chunk in use */
	CellChunk *spare;             /* one emptied chunk kept for reuse */
	Temp *free_temps;             /* released temporaries, reused before malloc */
	Value **globals;              /* one stable cell per Sema.globals entry */
	Temp **temps;
	int ntemps, captemps;
	int depth;
	int max_depth;        /* nested calls allowed (MAX_DEPTH, or fewer on a smaller stack) */
	Stmt *goto_target;            /* while unwinding for a goto */
	Arena lits;
	int argc;
	char **argv;
	Value argv_cell;
	bool keep_place;              /* eval_place: the place is to be lent (place_to_keep) */
} Interp;

static Str empty_str = { .len = 0, .is_static = true, .data = "" };

static Value v_void(void) { return (Value){ .kind = V_VOID }; }
static Value v_int(int64_t i) { return (Value){ .kind = V_INT, .as.i = i }; }
static Value v_float(double f) { return (Value){ .kind = V_FLOAT, .as.f = f }; }
static Value v_bool(bool b) { return (Value){ .kind = V_BOOL, .as.b = b }; }
static Value v_ref(Value *p) { return (Value){ .kind = V_REF, .as.ref = p }; }

static const char *file_of(SrcLoc l) { return diag_file_name(l.file); }

static _Thread_local int g_panic_file = -1;
static _Thread_local int g_lib_call_fileid = -1;   /* the file of the program's call into a library, if in one */

/* a header (.hby): a library's, or the program's own */
static bool is_header_file(int file)
{
	static const char *names[1024];   /* the name each answer was for (the session reuses numbers) */
	static bool known[1024];
	if (file < 0)
		return false;
	const char *n = diag_file_name(file);
	if (file < 1024 && names[file] == n)
		return known[file];
	size_t len = n ? strlen(n) : 0;
	bool h = len > 4 && strcmp(n + len - 4, ".hby") == 0;
	if (file < 1024) {
		names[file] = n;
		known[file] = h;
	}
	return h;
}

_Noreturn static void panic_at(SrcLoc l, const char *fmt, const char *arg)
{
	g_panic_file = l.file;
	rt_panic(file_of(l), l.line, l.col, fmt, arg);
}

_Noreturn static void internal_error(SrcLoc l, const char *what)
{
	panic_at(l, "internal interpreter error: %s", what);
}

static void set_panic_loc(SrcLoc l) { g_panic_file = l.file; }

/* a panic with any arguments, at l */
#define PANIC(l, ...) (set_panic_loc(l), rt_panic(file_of(l), (l).line, (l).col, __VA_ARGS__))

static Value *deref_all(Value *v)
{
	while (v->kind == V_REF)
		v = v->as.ref;
	return v;
}

static int64_t norm(int64_t v, const Type *t)
{
	if (t->kind == TY_BOOL)
		return v != 0;
	if (t->kind != TY_INT || t->bits == 64)
		return v;
	uint64_t mask = ((uint64_t)1 << t->bits) - 1;
	uint64_t u = (uint64_t)v & mask;
	if (t->is_signed && (u >> (t->bits - 1)))
		u |= ~mask;
	return (int64_t)u;
}

/* Short strings come and go all the time (a line, trimmed, sliced, let
 * go of), so their blocks are kept when they're let go of, and handed to
 * the next short string, rather than going back to malloc each time (as
 * Python keeps its own). Two sizes: 64 and 128 bytes, a Str and its text.
 * The interpreter's: one thread at a time runs Cabby code (the lock). */
#define STR_POOL_MOST 256
static void *g_str_pool[2][STR_POOL_MOST];
static int g_str_pooled[2];

static void *str_block(size_t need)
{
	int k = need <= 64 ? 0 : need <= 128 ? 1 : -1;
	if (k < 0)
		return rt_alloc(need);
	if (g_str_pooled[k])
		return g_str_pool[k][--g_str_pooled[k]];
	return rt_alloc(k == 0 ? 64 : 128);
}

/* a string's block, let go of: kept, if it's one of the sizes */
static void str_unblock(Str *s)
{
	size_t have = rt_usable_size(s);
	int k = have >= 128 && have < 192 ? 1 : have >= 64 && have < 96 ? 0 : -1;
	if (k < 0 || g_str_pooled[k] == STR_POOL_MOST) {
		rt_free(s);
		return;
	}
	g_str_pool[k][g_str_pooled[k]++] = s;
}

static void str_pool_free(void)
{
	for (int k = 0; k < 2; k++)
		while (g_str_pooled[k])
			rt_free(g_str_pool[k][--g_str_pooled[k]]);
}

static Str *str_new(const char *data, int64_t len)
{
	Str *s = str_block(sizeof *s + (size_t)len + 1);
	s->len = len;
	s->cap = len + 1;
	s->is_static = false;
	s->data = (char *)(s + 1);
	memcpy(s->data, data, (size_t)len);
	s->data[len] = '\0';
	return s;
}

static Value call_function(Interp *I, FnDecl *fn, Value *self, Value *args, SrcLoc loc);

/* ============================================================== zero values */


static _Thread_local Interp *g_interp;   /* the running interpreter, for handlers called from C */
static Interp *g_main_interp;            /* the program's own: a worker's thread starts from it */
static void release_temps(Interp *I, int mark);

/* What runs when an object is made (`rocket { }`), base-most class first */
static void run_makers(Object *ob)
{
	ObjectInfo *cls = ob->cls;
	if (!cls->has_maker || !g_interp)
		return;
	for (int i = 0; i < cls->layout_len; i++) {
		FnDecl *d = cls->layout[i]->decl->make_fn;
		if (d) {
			Value cell = { .kind = V_OBJECT, .as.obj = ob };
			Value self = v_ref(&cell);
			int mark = g_interp->ntemps;
			call_function(g_interp, d, &self, NULL, d->loc);
			release_temps(g_interp, mark);
		}
	}
}

/* ---- addresses (use addresses) ---- */

static Arena addr_arena;   /* AddrVals: small, never changed once made */

static Value make_address(Value *cell, Array *arr, int64_t idx, unsigned char *raw, const Type *elem)
{
	AddrVal *a = arena_alloc(&addr_arena, sizeof *a);
	a->cell = cell;
	a->arr = arr;
	a->idx = idx;
	a->raw = raw;
	a->elem = elem;
	a->list = NULL;
	a->watch = -1;
	return (Value){ .kind = V_ADDRESS, .as.ad = a };
}

/* the same address, at another place along */
static Value address_along(const AddrVal *a, int64_t idx)
{
	Value v = make_address(a->cell, a->arr, idx, a->raw, a->elem);
	v.as.ad->list = a->list;
	v.as.ad->watch = a->watch;
	return v;
}

/* An address of one of Cabby's own values is only good while the value is
 * there. Each place whose address is taken (a variable's cell, an array, an
 * element of a list) is watched, and when it goes (its function returns,
 * its block ends, it is dropped, or a list's items move) the watch says
 * so; using an address of it afterwards is a panic that says why. */
typedef struct Watch {
	const void *key;         /* the cell, or the Array */
	bool live;
	char what[96];           /* "'Count' in make_counter" */
	char why[96];            /* "make_counter returned" */
	const unsigned char *lo, *hi;   /* a string's characters (and its '\0'): the bounds */
	struct List *list;       /* a list: good while it keeps this length */
	int64_t len;
} Watch;
static Watch *g_watches;
static int g_nwatches, g_capwatches;
static int *g_live_watches;  /* the live ones, in the order of their keys (where they are) */
static int g_nlive_watches, g_caplive_watches;

/* where in g_live_watches the first with a key at or after p is */
static int live_watch_from(const void *p)
{
	int lo = 0, hi = g_nlive_watches;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if ((const char *)g_watches[g_live_watches[mid]].key < (const char *)p)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

static void live_watch_remove_at(int i)
{
	memmove(g_live_watches + i, g_live_watches + i + 1, sizeof *g_live_watches * (size_t)(g_nlive_watches - i - 1));
	g_nlive_watches--;
}

static int watch_place(const void *key, const char *what)
{
	int at = live_watch_from(key);
	if (at < g_nlive_watches && g_watches[g_live_watches[at]].key == key)
		return g_live_watches[at];
	if (g_nwatches == g_capwatches) {
		g_capwatches = g_capwatches ? g_capwatches * 2 : 16;
		g_watches = rt_realloc(g_watches, sizeof *g_watches * (size_t)g_capwatches);
	}
	if (g_nlive_watches == g_caplive_watches) {
		g_caplive_watches = g_caplive_watches ? g_caplive_watches * 2 : 16;
		g_live_watches = rt_realloc(g_live_watches, sizeof *g_live_watches * (size_t)g_caplive_watches);
	}
	Watch *w = &g_watches[g_nwatches];
	w->key = key;
	w->live = true;
	snprintf(w->what, sizeof w->what, "%s", what);
	w->why[0] = '\0';
	w->lo = w->hi = NULL;
	w->list = NULL;
	w->len = 0;
	memmove(g_live_watches + at + 1, g_live_watches + at, sizeof *g_live_watches * (size_t)(g_nlive_watches - at));
	g_live_watches[at] = g_nwatches;
	g_nlive_watches++;
	return g_nwatches++;
}

/* "'Count' in make_counter", "'Pt' in main", or "a global 'Total'" */
static void describe_place(Interp *I, Expr *x, char *buf, size_t n)
{
	Expr *root = x;
	while (root->kind == EXPR_FIELD || root->kind == EXPR_INDEX)
		root = root->kind == EXPR_FIELD ? root->as.field.object : root->as.index.base;
	const char *part = x->kind == EXPR_INDEX ? "an element of " : x->kind == EXPR_FIELD ? "part of " : "";
	if ((root->kind == EXPR_IDENT || root->kind == EXPR_SELF) && root->as.ident.sym) {
		Symbol *s = root->as.ident.sym;
		if (s->kind == SYM_GLOBAL)
			snprintf(buf, n, "%s'%s'", part, s->name);
		else
			snprintf(buf, n, "%s'%s' in %s", part, s->name, I->frame && I->frame->fn ? I->frame->fn->name : "main");
	} else {
		snprintf(buf, n, "a value");
	}
}

static void places_gone(const void *lo, const void *hi, const char *why);

/* A list changed length: addresses of its items are gone */
static void list_resized(List *l, const char *how)
{
	if (g_nlive_watches) {
		char why[96];
		snprintf(why, sizeof why, "the list changed length (%s)", how);
		places_gone(l, l + 1, why);
	}
}

/* An address of a list's item: good while the list keeps its length */
static Value list_address(List *l, int64_t idx, const Type *elem, const char *what)
{
	Value r = make_address(NULL, NULL, idx, NULL, elem);
	r.as.ad->list = l;
	int w = watch_place(l, what);
	if (!g_watches[w].list) {
		g_watches[w].list = l;
		g_watches[w].len = l->len;
	} else if (g_watches[w].len != l->len) {
		/* a new length since the last address of it: a new watch */
		g_watches[w].live = false;
		snprintf(g_watches[w].why, sizeof g_watches[w].why, "the list changed length");
		for (int i = live_watch_from(l); i < g_nlive_watches && g_watches[g_live_watches[i]].key == l; i++)
			if (g_live_watches[i] == w) {
				live_watch_remove_at(i);
				break;
			}
		w = watch_place(l, what);
		g_watches[w].list = l;
		g_watches[w].len = l->len;
	}
	r.as.ad->watch = w;
	return r;
}

/* Everything watched from lo up to (not including) hi is gone, because of `why`. */
static void places_gone(const void *lo, const void *hi, const char *why)
{
	int from = live_watch_from(lo), to = from;
	while (to < g_nlive_watches && (const char *)g_watches[g_live_watches[to]].key < (const char *)hi) {
		Watch *w = &g_watches[g_live_watches[to++]];
		w->live = false;
		snprintf(w->why, sizeof w->why, "%s", why);
	}
	if (to > from) {
		memmove(g_live_watches + from, g_live_watches + to, sizeof *g_live_watches * (size_t)(g_nlive_watches - to));
		g_nlive_watches -= to - from;
	}
}

static Value raw_address(void *p)
{
	if (!p)
		return (Value){ .kind = V_ADDRESS, .as.ad = NULL };
	return make_address(NULL, NULL, 0, p, NULL);
}

static bool address_is_none(const AddrVal *a) { return !a || (!a->cell && !a->arr && !a->list && !a->raw); }

static void check_alive(const AddrVal *a, SrcLoc l)
{
	if (!a || a->watch < 0)
		return;
	Watch *w = &g_watches[a->watch];
	if (w->live && w->list && w->list->len != w->len) {
		w->live = false;
		snprintf(w->why, sizeof w->why, "the list changed length (from %lld to %lld items)", (long long)w->len,
			 (long long)w->list->len);
	}
	if (!w->live)
		PANIC(l, "this address is of %s, which is gone: %s", w->what, w->why);
}

/* an array's or a list's elements, for an address along one */
static Value *seq_elems(const AddrVal *a) { return a->arr ? a->arr->elems : a->list->items; }
static int64_t seq_len(const AddrVal *a) { return a->arr ? a->arr->len : a->list->len; }

/* ---- memory by hand: every block from addresses.allocate, live or given
 * back. Given-back blocks are kept (not really freed) while the program
 * runs, so a stale address can never land in a new block unnoticed. */
/* Where a block was allocated or given back, worked out at the time: in
 * the interactive session the program is rebuilt after every input, so a
 * line number would mean nothing later; the line's own text is kept. */
typedef struct Where {
	char name[96];           /* the file, or "" for a line typed in the session */
	int line, col;
	char text[96];           /* in the session: what was typed */
} Where;

typedef struct MemBlock {
	unsigned char *p;
	uint64_t size;           /* bytes */
	bool live;
	bool reallocated;        /* given back by reallocate (not free) */
	Where made, gone;
	int site;                /* the allocating line, in g_sites */
	bool reached;            /* while looking for lost blocks */
	int left, right;         /* the blocks in order of where they are: a treap */
	unsigned prio;
} MemBlock;

/* Each line that allocates. When it allocates again while an earlier block
 * of its own is still live, the interpreter looks for that block among
 * everything the program holds: a block nothing holds any more is lost, and
 * is noted at once (a leak in a loop shows up as it happens). Each line is
 * noted once. Looking takes one pass over everything the program holds, so
 * a line whose blocks keep turning out to be held (a list being built) is
 * looked at less and less often: after 2, 4, 8, 16... allocations. */
typedef struct Site {
	Where w;
	int64_t allocs, next_look;          /* allocating again */
	int64_t overwrites, next_overwrite;  /* an address of one of its blocks overwritten */
	bool noted;
} Site;
static Site *g_sites;
static int g_nsites;

static const char *file_of(SrcLoc l);

static Where where_of(SrcLoc l)
{
	Where w = { "", l.line, l.col, "" };
	DiagPos m = diag_map(l.file, l.line, l.col);
	if (m.name && m.name[0] != '<') {
		snprintf(w.name, sizeof w.name, "%s", file_of(l));
		return w;
	}
	/* the session: the typed line, without its indent */
	const char *src = diag_file_source(l.file);
	if (!src)
		return w;
	const char *p = src;
	for (int k = 1; k < l.line && *p; p++)
		if (*p == '\n')
			k++;
	while (*p == '\t' || *p == ' ')
		p++;
	int n = 0;
	while (p[n] && p[n] != '\n')
		n++;
	if (n > (int)sizeof w.text - 4)
		snprintf(w.text, sizeof w.text, "%.*s...", (int)sizeof w.text - 4, p);
	else
		snprintf(w.text, sizeof w.text, "%.*s", n, p);
	return w;
}

/* "at prog.cby:7" (with the column if asked), or "in 'addresses.free A'" */
static const char *where_str(const Where *w, bool col, char *buf, size_t n)
{
	if (w->text[0])
		snprintf(buf, n, "in '%s'", w->text);
	else if (col)
		snprintf(buf, n, "at %s:%d:%d", w->name, w->line, w->col);
	else
		snprintf(buf, n, "at %s:%d", w->name, w->line);
	return buf;
}

/* "at line 4", or "in 'address int A = addresses.allocate 8'" */
static const char *where_short(const Where *w, char *buf, size_t n)
{
	if (w->text[0])
		snprintf(buf, n, "in '%s'", w->text);
	else
		snprintf(buf, n, "at line %d", w->line);
	return buf;
}
static MemBlock *g_blocks;
static int g_nblocks, g_capblocks;
static int g_block_root = -1;

/* Each block has this much room after its end, belonging to nobody, so
 * that stepping past the end lands somewhere the interpreter recognizes. */
#define GUARD_ZONE 256

/* Blocks never overlap (given-back ones are kept, so the system can't reuse
 * their memory), so the block holding p is the last one starting at or
 * before it, if p is within its size and guard zone. */
static MemBlock *block_at(const unsigned char *p)
{
	int best = -1;
	for (int i = g_block_root; i >= 0;) {
		if (g_blocks[i].p <= p) {
			best = i;
			i = g_blocks[i].right;
		} else {
			i = g_blocks[i].left;
		}
	}
	if (best >= 0 && p < g_blocks[best].p + g_blocks[best].size + GUARD_ZONE)
		return &g_blocks[best];
	return NULL;
}

static int treap_insert(int t, int n)
{
	if (t < 0)
		return n;
	if (g_blocks[n].p < g_blocks[t].p) {
		g_blocks[t].left = treap_insert(g_blocks[t].left, n);
		if (g_blocks[g_blocks[t].left].prio > g_blocks[t].prio) {
			int l = g_blocks[t].left;
			g_blocks[t].left = g_blocks[l].right;
			g_blocks[l].right = t;
			return l;
		}
	} else {
		g_blocks[t].right = treap_insert(g_blocks[t].right, n);
		if (g_blocks[g_blocks[t].right].prio > g_blocks[t].prio) {
			int r = g_blocks[t].right;
			g_blocks[t].right = g_blocks[r].left;
			g_blocks[r].left = t;
			return r;
		}
	}
	return t;
}

static int site_of(const Where *w)
{
	for (int i = g_nsites - 1; i >= 0; i--)
		if (g_sites[i].w.line == w->line && g_sites[i].w.col == w->col && strcmp(g_sites[i].w.name, w->name) == 0 &&
		    strcmp(g_sites[i].w.text, w->text) == 0)
			return i;
	g_sites = rt_realloc(g_sites, sizeof *g_sites * (size_t)(g_nsites + 1));
	g_sites[g_nsites] = (Site){ .w = *w, .next_look = 2, .next_overwrite = 1 };
	return g_nsites++;
}

static void add_block_at(unsigned char *p, uint64_t size, Where made, int site)
{
	static unsigned seed = 2463534242u;
	if (g_nblocks == g_capblocks) {
		g_capblocks = g_capblocks ? g_capblocks * 2 : 64;
		g_blocks = rt_realloc(g_blocks, sizeof *g_blocks * (size_t)g_capblocks);
	}
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	g_blocks[g_nblocks] = (MemBlock){ .p = p, .size = size, .live = true, .made = made, .site = site,
					  .left = -1, .right = -1, .prio = seed };
	g_block_root = treap_insert(g_block_root, g_nblocks);
	g_nblocks++;
}

static int add_block(unsigned char *p, uint64_t size, SrcLoc made)
{
	Where w = where_of(made);
	int site = site_of(&w);
	add_block_at(p, size, w, site);
	return site;
}

/* Reading or writing `n` bytes at p: checked against the blocks Cabby made */
static void check_memory(const unsigned char *p, uint64_t n, SrcLoc l)
{
	MemBlock *b = block_at(p);
	if (!b)
		return;   /* memory from C, or a fixed address: not Cabby's to check */
	char w1[160], w2[160];
	if (!b->live)
		PANIC(l, "this memory was given back by %s %s (allocated %s)%s",
		      b->reallocated ? "addresses.reallocate" : "addresses.free", where_str(&b->gone, false, w1, sizeof w1),
		      where_short(&b->made, w2, sizeof w2), b->reallocated ? ": use the address reallocate gave instead" : "");
	if (p + n > b->p + b->size)
		PANIC(l, "this address is past the end of the memory allocated %s (%llu bytes)",
		      where_str(&b->made, false, w1, sizeof w1), (unsigned long long)b->size);
}

/* ---- looking for lost blocks: which live blocks can the program still reach? */

static MemBlock **g_live;    /* live blocks, sorted by where they are */
static int g_nlive;
static MemBlock **g_work;    /* reached blocks whose own bytes are still to look through */
static int g_nwork;

static int by_place(const void *a, const void *b)
{
	const unsigned char *x = (*(MemBlock *const *)a)->p, *y = (*(MemBlock *const *)b)->p;
	return x < y ? -1 : x > y;
}

/* p might point into a live block (anywhere in it, or just past its end) */
static void reach(const unsigned char *p)
{
	int lo = 0, hi = g_nlive - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		MemBlock *b = g_live[mid];
		if (p < b->p)
			hi = mid - 1;
		else if (p > b->p + b->size)
			lo = mid + 1;
		else {
			if (!b->reached) {
				b->reached = true;
				g_work[g_nwork++] = b;
			}
			return;
		}
	}
}

static void reach_bytes(const unsigned char *p, uint64_t n)
{
	for (uint64_t i = 0; i + sizeof(void *) <= n; i += sizeof(void *)) {
		const unsigned char *q;
		memcpy(&q, p + i, sizeof q);
		reach(q);
	}
}

static void reach_value(const Value *v)
{
	switch (v->kind) {
	case V_ADDRESS:
		if (v->as.ad && v->as.ad->raw)
			reach(v->as.ad->raw);
		break;
	case V_ARRAY:
		if (!v->as.arr->raw)
			for (int64_t i = 0; i < v->as.arr->len; i++)
				reach_value(&v->as.arr->elems[i]);
		break;
	case V_OBJECT:
		for (int i = 0; i < v->as.obj->cls->field_count; i++)
			reach_value(&v->as.obj->fields[i]);
		break;
	case V_UNION:
		reach_bytes(v->as.un->bytes, v->as.un->size);
		break;
	case V_LIST:
		for (int64_t i = 0; i < v->as.list->len; i++)
			reach_value(&v->as.list->items[i]);
		break;
	case V_LOOKUP:
		for (int64_t p = 0; p < v->as.lk->n; p++)
			if (!v->as.lk->dead[p]) {
				reach_value(&v->as.lk->keys[p]);
				reach_value(&v->as.lk->vals[p]);
			}
		break;
	default:
		break;   /* a borrow points at a cell that is looked at where it is */
	}
}

/* Marks every live block the program can still reach, from its variables
 * (every call's, and the globals), its temporaries, and the bytes of the
 * blocks reached. Memory handed to C, and kept there, can't be seen. */
static void find_reachable(Interp *I)
{
	g_live = rt_alloc(sizeof *g_live * (size_t)(g_nblocks ? g_nblocks : 1));
	g_work = rt_alloc(sizeof *g_work * (size_t)(g_nblocks ? g_nblocks : 1));
	g_nlive = g_nwork = 0;
	for (int i = 0; i < g_nblocks; i++)
		if (g_blocks[i].live) {
			g_blocks[i].reached = false;
			g_live[g_nlive++] = &g_blocks[i];
		}
	qsort(g_live, (size_t)g_nlive, sizeof *g_live, by_place);
	for (Frame *f = I->frame; f; f = f->caller)
		for (int i = 0; i < f->ncells; i++)
			reach_value(&f->cells[i]);
	for (int i = 0; I->S && i < I->S->globals.len; i++)
		reach_value(I->globals[i]);
	for (int i = 0; i < I->ntemps; i++)
		reach_value(&I->temps[i]->cell);
	reach_value(&I->argv_cell);
	while (g_nwork) {
		MemBlock *b = g_work[--g_nwork];
		reach_bytes(b->p, b->size);
	}
	rt_free(g_live);
	rt_free(g_work);
}

static void explain_lost_note(void)
{
	static bool said;
	if (!said)
		fprintf(stderr, "  (each line is noted once; the end of the program lists every block never given back)\n");
	said = true;
}

/* A line allocating again: are its earlier blocks still held? */
static void look_for_lost(Interp *I, int site, const unsigned char *newest)
{
	Site *s = &g_sites[site];
	if (s->noted || ++s->allocs < s->next_look)
		return;
	int earlier = 0;
	for (int i = 0; i < g_nblocks; i++)
		earlier += g_blocks[i].live && g_blocks[i].site == site && g_blocks[i].p != newest;
	if (!earlier)
		return;
	find_reachable(I);
	int lost = 0;
	uint64_t bytes = 0;
	for (int i = 0; i < g_nblocks; i++)
		if (g_blocks[i].live && g_blocks[i].site == site && !g_blocks[i].reached && g_blocks[i].p != newest) {
			lost++;
			bytes += g_blocks[i].size;
		}
	if (!lost) {
		s->next_look = s->allocs * 2;   /* held: look again later */
		return;
	}
	s->noted = true;
	char w[160];
	fflush(stdout);
	if (lost == 1)
		fprintf(stderr, "note: memory allocated %s (%llu bytes) was lost before that line ran again: nothing "
			"holds its address any more, and it was never given back\n", where_str(&s->w, true, w, sizeof w),
			(unsigned long long)bytes);
	else
		fprintf(stderr, "note: %d blocks of memory allocated %s (%llu bytes) were lost before that line ran again: "
			"nothing holds their addresses any more, and they were never given back\n", lost,
			where_str(&s->w, true, w, sizeof w), (unsigned long long)bytes);
	explain_lost_note();
}

/* An address variable was just changed: if it held the start of a live
 * block, is that block still held by anything? */
static void look_after_overwrite(Interp *I, const unsigned char *was, SrcLoc at)
{
	MemBlock *b = block_at(was);
	if (!b || !b->live || b->p != was)
		return;
	Site *s = &g_sites[b->site];
	if (s->noted || ++s->overwrites < s->next_overwrite)
		return;
	int bi = (int)(b - g_blocks);
	find_reachable(I);
	b = &g_blocks[bi];
	if (b->reached) {
		s->next_overwrite = s->overwrites * 2;   /* held: look again later */
		return;
	}
	s->noted = true;
	char w1[160], w2[160];
	Where lost = where_of(at);
	fflush(stdout);
	fprintf(stderr, "note: memory allocated %s (%llu bytes) was lost %s: nothing holds its address any more, "
		"and it was never given back\n", where_str(&s->w, true, w1, sizeof w1), (unsigned long long)b->size,
		where_str(&lost, false, w2, sizeof w2));
	explain_lost_note();
}

/* At the end of the program: blocks never given back */
static void report_leaks(void)
{
	int n = 0;
	uint64_t bytes = 0;
	for (int i = 0; i < g_nblocks; i++)
		if (g_blocks[i].live) {
			n++;
			bytes += g_blocks[i].size;
		}
	if (n) {
		fflush(stdout);
		fprintf(stderr, "note: %d block%s from addresses.allocate never given back (%llu bytes):\n", n, n == 1 ? "" : "s",
			(unsigned long long)bytes);
		/* by the line that allocated them */
		for (int si = 0; si < g_nsites; si++) {
			int k = 0;
			uint64_t sb = 0;
			for (int i = 0; i < g_nblocks; i++)
				if (g_blocks[i].live && g_blocks[i].site == si) {
					k++;
					sb += g_blocks[i].size;
				}
			if (!k)
				continue;
			char w[160];
			if (k == 1)
				fprintf(stderr, "  %llu bytes allocated %s\n", (unsigned long long)sb,
					where_str(&g_sites[si].w, true, w, sizeof w));
			else
				fprintf(stderr, "  %llu bytes in %d blocks allocated %s\n", (unsigned long long)sb, k,
					where_str(&g_sites[si].w, true, w, sizeof w));
		}
	}
	for (int i = 0; i < g_nblocks; i++)
		free(g_blocks[i].p);
	free(g_blocks);
	g_blocks = NULL;
	g_nblocks = g_capblocks = 0;
	g_block_root = -1;
	rt_free(g_sites);
	g_sites = NULL;
	g_nsites = 0;
}

/* During a call to C: the copies of Cabby values C was given, so that an
 * address into one (given or given back) maps to the Cabby value itself. */
typedef struct Xlat {
	unsigned char *buf;
	uint64_t es;
	int64_t count;
	AddrVal *ad;
} Xlat;
static _Thread_local Xlat *g_xlat;
static _Thread_local int g_nxlat;

static Value make_address(Value *cell, Array *arr, int64_t idx, unsigned char *raw, const Type *elem);

static Value xlat_from_c(unsigned char *p)
{
	for (int i = 0; i < g_nxlat && p; i++) {
		Xlat *x = &g_xlat[i];
		if (p >= x->buf && p <= x->buf + x->es * (uint64_t)x->count && (uint64_t)(p - x->buf) % x->es == 0)
		{
			return address_along(x->ad, x->ad->idx + (int64_t)((uint64_t)(p - x->buf) / x->es));
		}
	}
	return (Value){ .kind = V_UNINIT };
}

static unsigned char *xlat_to_c(const AddrVal *a)
{
	for (int i = 0; a && i < g_nxlat; i++) {
		Xlat *x = &g_xlat[i];
		if ((a->arr && a->arr == x->ad->arr) || (a->list && a->list == x->ad->list) ||
		    (a->cell && a->cell == x->ad->cell)) {
			int64_t k = a->idx - x->ad->idx;
			if (k >= 0 && k <= x->count)
				return x->buf + (uint64_t)k * x->es;
		}
	}
	return NULL;
}

/* The address as a number: memory from C, or where the interpreter keeps
 * the value (stable while it lives, and in order along an array). */
static int64_t address_number(const AddrVal *a, const Type *el)
{
	if (address_is_none(a))
		return 0;
	if (a->raw)
		return (int64_t)(intptr_t)(a->raw + a->idx * (int64_t)(el ? type_size(el) : 1));
	if (a->arr || a->list)
		return (int64_t)(intptr_t)seq_elems(a) + a->idx * (int64_t)sizeof(Value);
	return (int64_t)(intptr_t)a->cell + a->idx * (int64_t)sizeof(Value);
}

static Value eval(Interp *I, Expr *e);

/* addresses.of A[i] or A.value, where A is an address: A stepped along */
static Value address_of_at(Interp *I, Expr *x)
{
	Value av = eval(I, x->as.at.address);
	int64_t step = x->as.at.step ? eval(I, x->as.at.step).as.i : 0;
	AddrVal *a = av.as.ad;
	if (address_is_none(a))
		return av;
	return address_along(a, a->idx + step);
}

static void encode(const Type *t, const Value *v, unsigned char *dst);
static Value decode(const Type *t, const unsigned char *src);

static int g_compact = -1;   /* CABBY_COMPACT=0 keeps every array as Values, to compare */

/* A new array of len elements of type inner: as bytes (all zero: 0,
 * false, 0.0) when they're numbers, else Values for the caller to fill. */
static Array *array_new(const Type *inner, int64_t len)
{
	if (g_compact < 0) {
		const char *c = getenv("CABBY_COMPACT");
		g_compact = !(c && !strcmp(c, "0"));
	}
	if (g_compact && inner && (inner->kind == TY_BOOL || inner->kind == TY_INT || inner->kind == TY_FLOAT)) {
		int64_t es = (int64_t)type_size(inner);
		Array *a = rt_alloc_zeroed(sizeof *a + (size_t)(es * len) + 1);
		a->len = len;
		a->raw = inner;
		a->es = es;
		return a;
	}
	Array *a = rt_alloc(sizeof *a + sizeof(Value) * (size_t)len);
	a->len = len;
	a->raw = NULL;
	a->es = 0;
	return a;
}

/* element i of an array of bytes, and changing it */
static inline Value raw_get(const Array *a, int64_t i)
{
	const unsigned char *p = ARR_BYTES(a) + i * a->es;
	if (a->raw->kind == TY_BOOL)
		return (Value){ .kind = V_BOOL, .as.b = *p != 0 };
	return decode(a->raw, p);
}

static inline void raw_put(Array *a, int64_t i, const Value *v)
{
	unsigned char *p = ARR_BYTES(a) + i * a->es;
	if (a->raw->kind == TY_BOOL)
		*p = v->as.b;
	else
		encode(a->raw, v, p);
}

/* The array in cell, spread out into Values, so that an element can be
 * lent or have an address: each keeps its place from then on. */
static void array_spread(Value *cell)
{
	Array *a = cell->as.arr;
	if (!a->raw)
		return;
	Array *n = rt_alloc(sizeof *n + sizeof(Value) * (size_t)a->len);
	n->len = a->len;
	n->raw = NULL;
	n->es = 0;
	for (int64_t i = 0; i < a->len; i++)
		n->elems[i] = raw_get(a, i);
	rt_free(a);
	cell->as.arr = n;
}

static Value zero_value(const Type *t)
{
	switch (t->kind) {
	case TY_BOOL:  return v_bool(false);
	case TY_INT:   return v_int(0);
	case TY_FLOAT: return v_float(0.0);
	case TY_STR:   return (Value){ .kind = V_STR, .as.s = &empty_str };
	case TY_HANDLER: return (Value){ .kind = V_HANDLER, .as.hv = NULL };   /* not set */
	case TY_ADDRESS: return (Value){ .kind = V_ADDRESS, .as.ad = NULL };   /* addresses.none */
	case TY_LINK:  return v_int(0);   /* nowhere */
	case TY_LIST: {
		List *l = rt_alloc(sizeof *l);
		l->len = l->cap = 0;
		l->items = NULL;
		return (Value){ .kind = V_LIST, .as.list = l };
	}
	case TY_LOOKUP:
		return (Value){ .kind = V_LOOKUP, .as.lk = rt_alloc_zeroed(sizeof(Lookup)) };
	case TY_FILE:
		return (Value){ .kind = V_FILE, .as.file = NULL };
	case TY_ARRAY: {
		Array *a = array_new(t->inner, (int64_t)t->len);
		if (!a->raw)
			for (uint64_t i = 0; i < t->len; i++)
				a->elems[i] = zero_value(t->inner);
		return (Value){ .kind = V_ARRAY, .as.arr = a };
	}
	case TY_OBJECT: {
		ObjectInfo *o = t->obj;
		if (o->kind == OBJ_UNION) {
			Union *u = rt_alloc_zeroed(sizeof *u + (size_t)o->size);
			u->cls = o;
			u->size = o->size;
			return (Value){ .kind = V_UNION, .as.un = u };
		}
		Object *ob = rt_alloc(sizeof *ob + sizeof(Value) * (size_t)o->field_count);
		ob->life = 0;
		ob->cls = o;
		for (int i = 0; i < o->field_count; i++)
			ob->fields[i] = zero_value(o->fields[i].type);
		run_makers(ob);
		return (Value){ .kind = V_OBJECT, .as.obj = ob };
	}
	default:
		return v_void();
	}
}

/* ============================================================== drop & copy */

/* As compiled (see cby_drop_nested): past a few hundred lists inside one
 * another, a list is set aside and dropped by the outermost drop, so a
 * chain of any length drops in a little stack. */
#define DROP_DEPTH 256
static _Thread_local int g_drop_depth;
static _Thread_local Value *g_set_aside;
static _Thread_local int64_t g_n_aside, g_cap_aside;

static void value_drop_now(Interp *I, Value *v);

static void value_drop(Interp *I, Value *v)
{
	if (v->kind == V_STR && !g_nlive_watches) {   /* (the commonest, straight: as value_drop_now does) */
		if (!v->as.s->is_static)
			str_unblock(v->as.s);
		v->kind = V_UNINIT;   /* (so it isn't let go of twice) */
		return;
	}
	if (v->kind != V_LIST && v->kind != V_LOOKUP) {
		value_drop_now(I, v);
		return;
	}
	if (g_drop_depth >= DROP_DEPTH) {
		if (g_n_aside == g_cap_aside) {
			g_cap_aside = g_cap_aside ? g_cap_aside * 2 : 64;
			g_set_aside = rt_realloc(g_set_aside, sizeof *g_set_aside * (size_t)g_cap_aside);
		}
		g_set_aside[g_n_aside++] = *v;
		v->kind = V_UNINIT;
		return;
	}
	g_drop_depth++;
	value_drop_now(I, v);
	if (g_drop_depth == 1) {
		while (g_n_aside) {
			Value x = g_set_aside[--g_n_aside];
			value_drop_now(I, &x);
		}
		rt_free(g_set_aside);
		g_set_aside = NULL;
		g_cap_aside = 0;
	}
	g_drop_depth--;
}

static void value_drop_now(Interp *I, Value *v)
{
	switch (v->kind) {
	case V_STR:
		if (g_nlive_watches)
			places_gone(v->as.s, v->as.s + 1, "the string was given a new value, or dropped");
		if (!v->as.s->is_static)
			str_unblock(v->as.s);
		break;
	case V_ARRAY: {
		Array *a = v->as.arr;
		if (!a->raw)
			for (int64_t i = 0; i < a->len; i++)
				value_drop(I, &a->elems[i]);
		if (g_nlive_watches)
			places_gone(a, a->raw ? (void *)(ARR_BYTES(a) + a->len * a->es + 1) : (void *)(a->elems + a->len + 1),
				    "it was dropped or replaced");
		rt_free(a);
		break;
	}
	case V_OBJECT: {
		Object *o = v->as.obj;
		ObjectInfo *cls = o->cls;
		if (cls->has_user_drop) {
			for (int i = cls->layout_len - 1; i >= 0; i--) {
				FnDecl *d = cls->layout[i]->decl->drop_fn;
				if (d) {
					Value self = v_ref(v);
					call_function(I, d, &self, NULL, d->loc);
				}
			}
		}
		/* (links to it are gone before its fields are: a field's destructor
		 * that follows one back finds it gone) */
		if (o->life) {
			cby_link_end(o->life);
			o->life = 0;
		}
		for (int i = 0; i < cls->field_count; i++)
			value_drop(I, &o->fields[i]);
		if (g_nlive_watches)
			places_gone(o->fields, o->fields + cls->field_count, "the object it was part of was dropped or replaced");
		rt_free(o);
		break;
	}
	case V_UNION:
		rt_free(v->as.un);
		break;
	case V_LIST: {
		List *l = v->as.list;
		for (int64_t i = 0; i < l->len; i++)
			value_drop(I, &l->items[i]);
		if (g_nlive_watches) {
			places_gone(l->items, l->items + l->cap, "its list was dropped");
			places_gone(l, l + 1, "the list was given a new value, or dropped");
		}
		rt_free(l->items);
		rt_free(l);
		break;
	}
	case V_FILE:
		rt_file_close(v->as.file);
		break;
	case V_LOOKUP: {
		Lookup *k = v->as.lk;
		for (int64_t p = 0; p < k->n; p++)
			if (!k->dead[p]) {
				value_drop(I, &k->keys[p]);
				value_drop(I, &k->vals[p]);
			}
		rt_free(k->keys);
		rt_free(k->vals);
		rt_free(k->dead);
		rt_free(k->index);
		rt_free(k);
		break;
	}
	default:
		break;
	}
	v->kind = V_UNINIT;
}

/* Frees a value's memory without running any destructor (after a panic). */
static void value_free(Value *v)
{
	switch (v->kind) {
	case V_STR:
		if (!v->as.s->is_static)
			str_unblock(v->as.s);
		break;
	case V_ARRAY:
		if (!v->as.arr->raw)
			for (int64_t i = 0; i < v->as.arr->len; i++)
				value_free(&v->as.arr->elems[i]);
		rt_free(v->as.arr);
		break;
	case V_OBJECT:
		for (int i = 0; i < v->as.obj->cls->field_count; i++)
			value_free(&v->as.obj->fields[i]);
		rt_free(v->as.obj);
		break;
	case V_UNION:
		rt_free(v->as.un);
		break;
	case V_LIST:
		for (int64_t i = 0; i < v->as.list->len; i++)
			value_free(&v->as.list->items[i]);
		rt_free(v->as.list->items);
		rt_free(v->as.list);
		break;
	case V_FILE:
		rt_file_close(v->as.file);
		break;
	case V_LOOKUP: {
		Lookup *k = v->as.lk;
		for (int64_t p = 0; p < k->n; p++)
			if (!k->dead[p]) {
				value_free(&k->keys[p]);
				value_free(&k->vals[p]);
			}
		rt_free(k->keys);
		rt_free(k->vals);
		rt_free(k->dead);
		rt_free(k->index);
		rt_free(k);
		break;
	}
	default:
		break;
	}
	v->kind = V_UNINIT;
}

/* Copy of a Copy-typed value (aggregates are deep-copied). */
static Value value_copy(const Value *v, SrcLoc loc)
{
	switch (v->kind) {
	case V_ARRAY: {
		const Array *a = v->as.arr;
		if (a->raw) {
			Array *n = rt_alloc(sizeof *n + (size_t)(a->es * a->len) + 1);
			memcpy(n, a, sizeof *n + (size_t)(a->es * a->len));
			return (Value){ .kind = V_ARRAY, .as.arr = n };
		}
		Array *n = rt_alloc(sizeof *n + sizeof(Value) * (size_t)a->len);
		n->len = a->len;
		n->raw = NULL;
		n->es = 0;
		for (int64_t i = 0; i < a->len; i++)
			n->elems[i] = value_copy(&a->elems[i], loc);
		return (Value){ .kind = V_ARRAY, .as.arr = n };
	}
	case V_OBJECT: {
		const Object *o = v->as.obj;
		if (o->cls->kind == OBJ_OBJECT)
			internal_error(loc, "attempt to copy an object");
		Object *n = rt_alloc(sizeof *n + sizeof(Value) * (size_t)o->cls->field_count);
		n->cls = o->cls;
		n->life = 0;
		for (int i = 0; i < o->cls->field_count; i++)
			n->fields[i] = value_copy(&o->fields[i], loc);
		return (Value){ .kind = V_OBJECT, .as.obj = n };
	}
	case V_UNION: {
		Union *u = rt_alloc(sizeof *u + (size_t)v->as.un->size);
		memcpy(u, v->as.un, sizeof *u + (size_t)v->as.un->size);
		return (Value){ .kind = V_UNION, .as.un = u };
	}
	case V_STR:
		if (v->as.s->is_static)
			return *v;   /* const str globals: the literal itself */
		internal_error(loc, "attempt to copy an owned string");
	case V_UNINIT:
		internal_error(loc, "use of an empty (moved) cell");
	default:
		return *v;
	}
}

/* ---- lists ---- */

static List *list_of(Value *v) { return deref_all(v)->as.list; }

/* L.sort: how two elements compare (bools are V_BOOL, everything else by kind) */
static int64_t sort_int(const Value *v) { return v->kind == V_BOOL ? v->as.b : v->as.i; }
static int cmp_value_signed(const void *a, const void *b)
{
	int64_t x = sort_int(a), y = sort_int(b);
	return (x > y) - (x < y);
}
static int cmp_value_unsigned(const void *a, const void *b)
{
	uint64_t x = (uint64_t)sort_int(a), y = (uint64_t)sort_int(b);
	return (x > y) - (x < y);
}
static int cmp_value_char(const void *a, const void *b)
{
	uint8_t x = (uint8_t)((const Value *)a)->as.i, y = (uint8_t)((const Value *)b)->as.i;
	return (x > y) - (x < y);
}
static int cmp_value_float(const void *a, const void *b)
{
	double x = ((const Value *)a)->as.f, y = ((const Value *)b)->as.f;
	if (isnan(x) || isnan(y))
		return isnan(x) - isnan(y);
	return (x > y) - (x < y);
}
static int cmp_value_str(const void *a, const void *b)
{
	const Str *x = ((const Value *)a)->as.s, *y = ((const Value *)b)->as.s;
	return rt_str_compare(x->data, x->len, y->data, y->len);
}

/* how keys of type t compare, as L.sort does */
static int (*cmp_for(const Type *t))(const void *, const void *)
{
	return t->kind == TY_STR ? cmp_value_str : t->kind == TY_FLOAT ? cmp_value_float :
	       t->kind == TY_INT && t->is_char ? cmp_value_char :
	       t->kind == TY_INT && !t->is_signed ? cmp_value_unsigned : cmp_value_signed;
}

/* L.sort_by and L.sort_with: a merge sort of the positions (so it's
 * stable: equals keep their order), then the elements put in that order */
typedef struct {
	Interp *I;
	List *l;
	Value *keys;                                   /* sort_by: each one's key */
	int (*cmp)(const void *, const void *);
	FnDecl *with;                                  /* sort_with: the function */
	SrcLoc loc;
} SortBy;

/* true when position b's element goes before a's */
static bool sort_before(SortBy *sb, int64_t b, int64_t a)
{
	if (sb->keys)
		return sb->cmp(&sb->keys[b], &sb->keys[a]) < 0;
	Value args[2] = { v_ref(&sb->l->items[b]), v_ref(&sb->l->items[a]) };
	Value r = call_function(sb->I, sb->with, NULL, args, sb->loc);
	return r.as.b;
}

static void sort_merge(SortBy *sb, int64_t *idx, int64_t *tmp, int64_t n)
{
	if (n < 2)
		return;
	int64_t h = n / 2;
	sort_merge(sb, idx, tmp, h);
	sort_merge(sb, idx + h, tmp, n - h);
	int64_t i = 0, j = h, k = 0;
	while (i < h && j < n)
		tmp[k++] = sort_before(sb, idx[j], idx[i]) ? idx[j++] : idx[i++];
	while (i < h)
		tmp[k++] = idx[i++];
	while (j < n)
		tmp[k++] = idx[j++];
	memcpy(idx, tmp, sizeof *idx * (size_t)n);
}

static void list_sort_by(Interp *I, List *l, Expr *cb, bool with, SrcLoc loc)
{
	int64_t n = l->len;
	if (n < 2)
		return;
	SortBy sb = { I, l, NULL, NULL, with ? cb->as.callback.fn : NULL, loc };
	if (!with) {
		sb.keys = malloc(sizeof(Value) * (size_t)n);
		sb.cmp = cmp_for(cb->as.callback.key);
		for (int64_t i = 0; i < n; i++) {
			if (cb->as.callback.fn) {
				Value arg = v_ref(&l->items[i]);
				sb.keys[i] = call_function(I, cb->as.callback.fn, NULL, &arg, loc);
			} else {
				Object *o = deref_all(&l->items[i])->as.obj;
				sb.keys[i] = o->fields[o->cls->block_start[cb->as.callback.owner_id] + cb->as.callback.field_index];
			}
		}
	}
	int64_t *idx = malloc(sizeof *idx * (size_t)n * 2);
	for (int64_t i = 0; i < n; i++)
		idx[i] = i;
	sort_merge(&sb, idx, idx + n, n);
	Value *was = malloc(sizeof(Value) * (size_t)n);
	memcpy(was, l->items, sizeof(Value) * (size_t)n);
	for (int64_t i = 0; i < n; i++)
		l->items[i] = was[idx[i]];
	if (sb.keys && cb->as.callback.fn)
		for (int64_t i = 0; i < n; i++)
			value_drop(I, &sb.keys[i]);
	free(sb.keys);
	free(was);
	free(idx);
}

/* Room for `more` elements at the end: the room doubles when full. */
static void list_reserve(List *l, int64_t more)
{
	if (l->len + more <= l->cap)
		return;
	int64_t cap = l->cap ? l->cap * 2 : 4;
	while (cap < l->len + more)
		cap *= 2;
	if (g_nlive_watches) {
		places_gone(l->items, l->items + l->cap, "its list grew, so its items moved");
		places_gone(l, l + 1, "the list grew, so its items moved");
	}
	Value *n = realloc(l->items, sizeof(Value) * (size_t)cap);
	if (!n)
		rt_panic(NULL, 0, 0, "out of memory");
	l->items = n;
	l->cap = cap;
}

/* ---- lookups ---- */

static Lookup *lookup_of(Value *v) { return deref_all(v)->as.lk; }

static uint64_t lk_hash(const Value *k)
{
	if (k->kind == V_STR)
		return rt_hash_bytes(k->as.s->data, k->as.s->len);
	if (k->kind == V_UNINIT)   /* an empty string */
		return rt_hash_bytes("", 0);
	return rt_hash_u64((uint64_t)(k->kind == V_BOOL ? k->as.b : k->as.i));
}

static bool lk_equal(const Value *a, const Value *b)
{
	if (a->kind == V_STR || b->kind == V_STR || a->kind == V_UNINIT || b->kind == V_UNINIT) {
		const Str *x = a->kind == V_STR ? a->as.s : &empty_str, *y = b->kind == V_STR ? b->as.s : &empty_str;
		return x->len == y->len && memcmp(x->data, y->data, (size_t)x->len) == 0;
	}
	if (a->kind == V_BOOL)
		return a->as.b == b->as.b;
	return a->as.i == b->as.i;
}

typedef struct { const Lookup *k; const Value *key; } LkFind;

static bool lk_same(const void *ctx, int64_t pos)
{
	const LkFind *f = ctx;
	return !f->k->dead[pos] && lk_equal(&f->k->keys[pos], f->key);
}

static int64_t lk_find(const Lookup *k, const Value *key)
{
	if (k->len == 0)
		return -1;
	LkFind f = { k, key };
	return rt_index_find(k->index, k->icap, lk_hash(key), lk_same, &f);
}

static void lk_reindex(Lookup *k)
{
	int64_t icap = rt_index_size(k->cap);
	if (icap != k->icap) {
		rt_free(k->index);
		k->index = rt_alloc(sizeof(int32_t) * (size_t)icap);
		k->icap = icap;
	}
	memset(k->index, 0, sizeof(int32_t) * (size_t)icap);
	for (int64_t p = 0; p < k->n; p++)
		if (!k->dead[p])
			rt_index_add(k->index, icap, lk_hash(&k->keys[p]), p);
}

/* The position for key's value: a new entry (a copy of the key, its value
 * cell empty) when it isn't there yet. */
static int64_t lk_put(Lookup *k, const Value *key, bool *is_new)
{
	int64_t pos = lk_find(k, key);
	*is_new = pos < 0;
	if (pos >= 0)
		return pos;
	if (k->n == k->cap) {
		if (k->n > k->len && k->n - k->len >= k->n / 2) {   /* at least half removed */
			int64_t to = 0;
			for (int64_t p = 0; p < k->n; p++)
				if (!k->dead[p]) {
					k->keys[to] = k->keys[p];
					k->vals[to] = k->vals[p];
					k->dead[to++] = 0;
				}
			k->n = to;
		} else {
			int64_t cap = k->cap ? k->cap * 2 : 8;
			k->keys = rt_realloc(k->keys, sizeof(Value) * (size_t)cap);
			k->vals = rt_realloc(k->vals, sizeof(Value) * (size_t)cap);
			k->dead = rt_realloc(k->dead, (size_t)cap);
			k->cap = cap;
		}
		lk_reindex(k);
	}
	pos = k->n++;
	k->keys[pos] = key->kind == V_STR ? (Value){ .kind = V_STR, .as.s = str_new(key->as.s->data, key->as.s->len) }
		     : key->kind == V_UNINIT ? (Value){ .kind = V_STR, .as.s = &empty_str } : *key;
	k->vals[pos].kind = V_UNINIT;
	k->dead[pos] = 0;
	k->len++;
	rt_index_add(k->index, k->icap, lk_hash(key), pos);
	return pos;
}

/* Takes out the entry at pos, dropping its key and value. */
static void lk_kill(Interp *I, Lookup *k, int64_t pos)
{
	value_drop(I, &k->keys[pos]);
	value_drop(I, &k->vals[pos]);
	k->dead[pos] = 1;
	k->len--;
	if (k->len == 0) {
		k->n = 0;
		memset(k->index, 0, sizeof(int32_t) * (size_t)k->icap);
	}
}

static _Noreturn void lk_missing(const Value *key, const Type *kt, SrcLoc l)
{
	set_panic_loc(l);
	if (kt->kind == TY_STR) {
		const Str *s = key->kind == V_STR ? key->as.s : &empty_str;
		rt_lookup_missing_str(s->data, s->len, file_of(l), l.line, l.col);
	}
	int kind = kt->kind == TY_BOOL ? RT_KEY_BOOL : kt->is_char ? RT_KEY_CHAR : kt->is_signed ? RT_KEY_INT : RT_KEY_UINT;
	rt_lookup_missing_int(kt->kind == TY_BOOL ? key->as.b : key->as.i, kind, file_of(l), l.line, l.col);
}

static Value deep_copy(const Value *v, SrcLoc loc);

/* .copy: in order, with no gaps */
static Value lk_copy(const Lookup *k, SrcLoc loc)
{
	Lookup *n = rt_alloc_zeroed(sizeof *n);
	if (k->len) {
		n->cap = k->len;
		n->keys = rt_alloc(sizeof(Value) * (size_t)n->cap);
		n->vals = rt_alloc(sizeof(Value) * (size_t)n->cap);
		n->dead = rt_alloc_zeroed((size_t)n->cap);
		for (int64_t p = 0; p < k->n; p++)
			if (!k->dead[p]) {
				n->keys[n->n] = deep_copy(&k->keys[p], loc);
				n->vals[n->n++] = deep_copy(&k->vals[p], loc);
			}
		n->len = n->n;
		lk_reindex(n);
	}
	return (Value){ .kind = V_LOOKUP, .as.lk = n };
}

/* A deep copy, for .copy and slices: strings and inner lists are copied too
 * (sema allows only plain values, strings and lists of those). */
static Value deep_copy(const Value *v, SrcLoc loc)
{
	if (v->kind == V_LOOKUP)
		return lk_copy(v->as.lk, loc);
	if (v->kind == V_STR)
		return (Value){ .kind = V_STR, .as.s = str_new(v->as.s->data, v->as.s->len) };
	if (v->kind == V_LIST) {
		const List *l = v->as.list;
		List *n = rt_alloc(sizeof *n);
		n->len = n->cap = l->len;
		n->items = l->len ? rt_alloc(sizeof(Value) * (size_t)l->len) : NULL;
		for (int64_t i = 0; i < l->len; i++)
			n->items[i] = deep_copy(&l->items[i], loc);
		return (Value){ .kind = V_LIST, .as.list = n };
	}
	return value_copy(v, loc);
}

/* Equal, for .contains and .find: numbers, bools, chars and strings. */
static bool same_value(const Value *a, const Value *b)
{
	if (a->kind == V_STR)
		return a->as.s->len == b->as.s->len && memcmp(a->as.s->data, b->as.s->data, (size_t)a->as.s->len) == 0;
	if (a->kind == V_FLOAT)
		return a->as.f == b->as.f;
	if (a->kind == V_BOOL)
		return a->as.b == b->as.b;
	return a->as.i == b->as.i;
}

static void list_index_check(int64_t i, int64_t len, bool insert, SrcLoc l)
{
	if (i < 0 || i > len || (i == len && !insert)) {
		set_panic_loc(l);
		rt_list_bounds(i, len, insert, file_of(l), l.line, l.col);
	}
}

/* ============================================================== plain bytes (unions, C calls) */

static void encode(const Type *t, const Value *v, unsigned char *dst)
{
	switch (t->kind) {
	case TY_CALLBACK: { int64_t x = v->as.i; memcpy(dst, &x, 8); break; }   /* a C function's address */
	case TY_BOOL: dst[0] = v->as.b; break;
	case TY_INT: {
		int64_t x = v->as.i;
		switch (t->bits) {
		case 8:  { int8_t y = (int8_t)x;  memcpy(dst, &y, 1); break; }
		case 16: { int16_t y = (int16_t)x; memcpy(dst, &y, 2); break; }
		case 32: { int32_t y = (int32_t)x; memcpy(dst, &y, 4); break; }
		default: memcpy(dst, &x, 8); break;
		}
		break;
	}
	case TY_FLOAT:
		if (t->bits == 32) {
			float f = (float)v->as.f;
			memcpy(dst, &f, 4);
		} else {
			memcpy(dst, &v->as.f, 8);
		}
		break;
	case TY_ARRAY: {
		uint64_t es = type_size(t->inner);
		const Array *a = v->as.arr;
		if (a->raw) {
			memcpy(dst, ARR_BYTES(a), (size_t)(es * (uint64_t)a->len));
			break;
		}
		for (int64_t i = 0; i < a->len; i++)
			encode(t->inner, &a->elems[i], dst + (uint64_t)i * es);
		break;
	}
	case TY_LINK:
		memcpy(dst, &v->as.i, 8);
		break;
	case TY_ADDRESS: {
		/* memory from C, or the copy of a Cabby value C is being given */
		AddrVal *a = v->kind == V_ADDRESS ? v->as.ad : NULL;
		void *p = a && a->raw ? a->raw + a->idx * (int64_t)(t->inner ? type_size(t->inner) : 1) : xlat_to_c(a);
		memcpy(dst, &p, sizeof p);
		break;
	}
	case TY_OBJECT:
		if (t->obj->kind == OBJ_UNION) {
			memcpy(dst, v->as.un->bytes, (size_t)t->obj->size);
		} else {
			memset(dst, 0, (size_t)t->obj->size);
			for (int i = 0; i < t->obj->field_count; i++)
				encode(t->obj->fields[i].type, &v->as.obj->fields[i], dst + t->obj->fields[i].offset);
		}
		break;
	default:
		break;
	}
}

static Value decode(const Type *t, const unsigned char *src)
{
	switch (t->kind) {
	case TY_LINK: {
		int64_t l;
		memcpy(&l, src, 8);
		return v_int(l);
	}
	case TY_ADDRESS: {
		void *p;
		memcpy(&p, src, sizeof p);
		Value in = xlat_from_c(p);
		return in.kind == V_ADDRESS ? in : raw_address(p);
	}
	case TY_BOOL: return v_bool(src[0] != 0);
	case TY_INT: {
		int64_t x = 0;
		switch (t->bits) {
		case 8:  { int8_t y;  memcpy(&y, src, 1); x = y; break; }
		case 16: { int16_t y; memcpy(&y, src, 2); x = y; break; }
		case 32: { int32_t y; memcpy(&y, src, 4); x = y; break; }
		default: memcpy(&x, src, 8); break;
		}
		return v_int(norm(x, t));
	}
	case TY_FLOAT:
		if (t->bits == 32) {
			float f;
			memcpy(&f, src, 4);
			return v_float(f);
		} else {
			double d;
			memcpy(&d, src, 8);
			return v_float(d);
		}
	case TY_ARRAY: {
		Array *a = array_new(t->inner, (int64_t)t->len);
		uint64_t es = type_size(t->inner);
		if (a->raw)
			memcpy(ARR_BYTES(a), src, (size_t)(es * t->len));
		else
			for (uint64_t i = 0; i < t->len; i++)
				a->elems[i] = decode(t->inner, src + i * es);
		return (Value){ .kind = V_ARRAY, .as.arr = a };
	}
	case TY_OBJECT: {
		ObjectInfo *o = t->obj;
		if (o->kind == OBJ_UNION) {
			Union *u = rt_alloc(sizeof *u + (size_t)o->size);
			u->cls = o;
			u->size = o->size;
			memcpy(u->bytes, src, (size_t)o->size);
			return (Value){ .kind = V_UNION, .as.un = u };
		}
		Object *ob = rt_alloc(sizeof *ob + sizeof(Value) * (size_t)o->field_count);
		ob->life = 0;
		ob->cls = o;
		for (int i = 0; i < o->field_count; i++)
			ob->fields[i] = decode(o->fields[i].type, src + o->fields[i].offset);
		return (Value){ .kind = V_OBJECT, .as.obj = ob };
	}
	default:
		return v_void();
	}
}

/* ============================================================== temporaries */

static Temp *push_temp(Interp *I, Value v)
{
	if (I->ntemps == I->captemps) {
		I->captemps = I->captemps ? I->captemps * 2 : 32;
		Temp **n = realloc(I->temps, sizeof *n * (size_t)I->captemps);
		if (!n)
			rt_panic(NULL, 0, 0, "out of memory");
		I->temps = n;
	}
	Temp *t = I->free_temps;
	if (t)
		I->free_temps = t->next_free;
	else
		t = rt_alloc(sizeof *t);
	t->cell = v;
	t->union_bytes = NULL;
	t->proxy_type = NULL;
	t->proxy_was = NULL;
	I->temps[I->ntemps++] = t;
	return t;
}

static unsigned char *proxy_snapshot(const unsigned char *p, size_t n)
{
	unsigned char *c = malloc(n ? n : 1);
	if (c)
		memcpy(c, p, n);
	return c;
}

static Value *new_temp(Interp *I, Value v) { return &push_temp(I, v)->cell; }

static void release_temps(Interp *I, int mark)
{
	/* Pop before dropping: a destructor may create temporaries itself. */
	while (I->ntemps > mark) {
		Temp *t = I->temps[--I->ntemps];
		if (t->union_bytes) {
			/* Write the stand-in back, but only if it was changed: a stand-in
			 * that was only read mustn't undo a change made through another
			 * one in the same statement ('P.value.B = P.value.A'). */
			size_t n = type_size(t->proxy_type);
			unsigned char here[8];
			unsigned char *now = n <= sizeof here ? here : calloc(n ? n : 1, 1);
			memset(now, 0, n <= sizeof here ? sizeof here : 0);
			encode(t->proxy_type, &t->cell, now);
			if (!t->proxy_was || memcmp(now, t->proxy_was, n) != 0)
				memcpy(t->union_bytes, now, n);
			if (now != here)
				free(now);
			if (t->proxy_was != t->was_here)
				free(t->proxy_was);
			t->proxy_was = NULL;
			t->union_bytes = NULL;
		}
		value_drop(I, &t->cell);
		t->next_free = I->free_temps;
		I->free_temps = t;
	}
}

/* ============================================================== expressions */

static Value eval(Interp *I, Expr *e);
static Exec exec_block(Interp *I, Block *b);

static bool is_place(const Expr *e)
{
	switch (e->kind) {
	case EXPR_IDENT: case EXPR_SELF:
		return e->as.ident.sym != NULL;
	case EXPR_FIELD: case EXPR_INDEX: case EXPR_AT: case EXPR_LINK_GET:
		return true;
	default:
		return false;
	}
}

static Value *eval_read(Interp *I, Expr *e);

static Value *symbol_cell(Interp *I, Symbol *s)
{
	Value *c = s->kind == SYM_GLOBAL ? I->globals[s->index] : &I->frame->cells[s->index];
	if (s->type->kind == TY_REF || s->type->kind == TY_SLICE)
		return c->as.ref;   /* a borrow: the place is what it points at */
	return c;
}

/* element e (an EXPR_INDEX) of base: a lookup's, a list's or an array's */
static Value *index_place(Interp *I, Expr *e, Value *base, bool keep)
{
	if (base->kind == V_LOOKUP) {
		Lookup *k = base->as.lk;
		if (e->as.index.lookup_part) {
			int64_t pos = eval(I, e->as.index.index).as.i;
			return e->as.index.lookup_part == 1 ? &k->keys[pos] : &k->vals[pos];
		}
		Value *key = deref_all(eval_read(I, e->as.index.index));
		int64_t pos = lk_find(k, key);
		if (pos < 0)
			lk_missing(key, e->as.index.base->ty->key, e->loc);
		return &k->vals[pos];
	}
	int64_t idx = eval(I, e->as.index.index).as.i;
	if (base->kind == V_LIST) {
		List *l = base->as.list;
		list_index_check(idx, l->len, false, e->loc);
		return &l->items[idx];
	}
	Array *a = base->as.arr;
	set_panic_loc(e->loc);
	rt_bounds_check(idx, a->len, file_of(e->loc), e->loc.line, e->loc.col);
	if (a->raw) {
		if (keep) {   /* to be lent, or have an address: spread out first */
			array_spread(base);
			return &base->as.arr->elems[idx];
		}
		/* a stand-in, written back when the statement ends */
		unsigned char *p = ARR_BYTES(a) + idx * a->es;
		Temp *t = push_temp(I, raw_get(a, idx));
		t->union_bytes = p;
		t->proxy_type = (Type *)a->raw;
		memcpy(t->was_here, p, (size_t)a->es);
		t->proxy_was = t->was_here;
		return &t->cell;
	}
	return &a->elems[idx];
}

static Value *link_get(Interp *I, Expr *e);

static Value *eval_place(Interp *I, Expr *e)
{
	switch (e->kind) {
	case EXPR_IDENT:
	case EXPR_SELF:
		return symbol_cell(I, e->as.ident.sym);
	case EXPR_AT: {
		/* what's at an address: checked where the interpreter can */
		Value av = eval(I, e->as.at.address);
		int64_t step = e->as.at.step ? eval(I, e->as.at.step).as.i : 0;
		AddrVal *a = av.as.ad;
		if (address_is_none(a))
			PANIC(e->loc, "there is nothing at this address: it is addresses.none");
		check_alive(a, e->loc);
		int64_t idx = a->idx + step;
		if ((a->arr || a->list || a->cell) && a->elem && !type_eq(a->elem, e->ty)) {
			char was[64], now[64];
			PANIC(e->loc, "this address was made for '%s', and the interpreter can't read it as '%s' (compiled, "
			      "it reads the memory as it is)", type_str(a->elem, was, sizeof was), type_str(e->ty, now, sizeof now));
		}
		if (a->arr || a->list) {
			if (idx < 0 || idx >= seq_len(a))
				PANIC(e->loc, "this address is %s %s of %" PRId64 " (at %" PRId64 ")",
					 idx < 0 ? "before the start of" : "past the end of", a->arr ? "an array" : "a list",
					 seq_len(a), idx);
			return &seq_elems(a)[idx];
		}
		if (a->cell) {
			if (idx != 0)
				PANIC(e->loc, "this address was stepped %" PRId64 " along from a single value, where "
					 "there is nothing of Cabby's", idx);
			return a->cell;
		}
		/* memory from C: a stand-in, written back when the statement ends */
		unsigned char *p = a->raw + idx * (int64_t)type_size(e->ty);
		if (a->watch >= 0 && g_watches[a->watch].hi) {
			Watch *w = &g_watches[a->watch];
			if (p < w->lo || p + type_size(e->ty) > w->hi)
				PANIC(e->loc, "this address is %s the text: it has %lld character%s, and the '\\0' after them",
				      p < w->lo ? "before the start of" : "past the end of", (long long)(w->hi - w->lo - 1),
				      w->hi - w->lo - 1 == 1 ? "" : "s");
		}
		check_memory(p, type_size(e->ty), e->loc);
		Temp *t = push_temp(I, decode(e->ty, p));
		t->union_bytes = p;
		t->proxy_type = e->ty;
		t->proxy_was = proxy_snapshot(p, type_size(e->ty));
		return &t->cell;
	}
	case EXPR_LINK_GET:
		return link_get(I, e);
	case EXPR_FIELD: {
		Value *base = deref_all(eval_read(I, e->as.field.object));
		if (base->kind == V_UNION) {
			Union *u = base->as.un;
			FieldInfo *f = &u->cls->fields[e->as.field.field_index];
			Temp *t = push_temp(I, decode(f->type, u->bytes + f->offset));
			t->union_bytes = u->bytes + f->offset;
			t->proxy_type = f->type;
			t->proxy_was = proxy_snapshot(t->union_bytes, type_size(f->type));
			return &t->cell;
		}
		Object *o = base->as.obj;
		if (e->as.field.worker_check)   /* (a worker's, from outside it: not while it works) */
			rt_worker_check(o, file_of(e->loc), e->loc.line, e->loc.col);
		int slot = o->cls->block_start[e->as.field.owner_id] + e->as.field.field_index;
		return &o->fields[slot];
	}
	case EXPR_INDEX: {
		bool keep = I->keep_place;   /* (for this one, not those in it) */
		I->keep_place = false;
		return index_place(I, e, deref_all(eval_read(I, e->as.index.base)), keep);
	}
	default:
		break;
	}
	internal_error(e->loc, "not a place expression");
}

/* A place that's to be lent, or have an address: one that stays where it
 * is, rather than a stand-in (an array of numbers is spread out). */
static Value *place_to_keep(Interp *I, Expr *e)
{
	I->keep_place = true;
	Value *c = eval_place(I, e);
	I->keep_place = false;
	return c;
}

static Value *eval_read(Interp *I, Expr *e)
{
	if ((e->kind == EXPR_IDENT || e->kind == EXPR_SELF) && e->as.ident.sym)
		return symbol_cell(I, e->as.ident.sym);   /* (the usual case, straight there) */
	if (is_place(e))
		return eval_place(I, e);
	return new_temp(I, eval(I, e));
}

/* literals and handlers last as long as the program: in its own
 * interpreter's arena, even when a worker's thread made them (under the
 * lock, one thread at a time) */
static Arena *lasting(Interp *I) { return g_main_interp ? &g_main_interp->lits : &I->lits; }

static Value string_literal(Interp *I, Expr *e)
{
	if (e->to_owned_str)
		return (Value){ .kind = V_STR, .as.s = str_new(e->as.str.data, (int64_t)e->as.str.len) };
	if (!e->backend) {
		Str *s = arena_alloc(lasting(I), sizeof *s);
		s->len = (int64_t)e->as.str.len;
		s->is_static = true;
		/* copied, so the value never points into the syntax tree (the
		 * interactive session frees old trees) */
		char *d = arena_alloc(lasting(I), e->as.str.len + 1);
		memcpy(d, e->as.str.data, e->as.str.len);
		d[e->as.str.len] = '\0';
		s->data = d;
		e->backend = s;
	}
	return (Value){ .kind = V_STR, .as.s = e->backend };
}

/* Converts a value of type `from` to type `to` (C semantics, saturating
 * float -> int). */
static Value convert(Value v, const Type *from, const Type *to)
{
	if (from == to || !from || !to || type_eq(from, to))
		return v;
	switch (to->kind) {
	case TY_BOOL:
		if (v.kind == V_FLOAT) return v_bool(v.as.f != 0);
		if (v.kind == V_INT)   return v_bool(v.as.i != 0);
		return v;
	case TY_INT:
		if (v.kind == V_FLOAT) return v_int(rt_float_to_int(v.as.f, to->bits, to->is_signed));
		if (v.kind == V_BOOL)  return v_int(v.as.b);
		if (v.kind == V_INT)   return v_int(norm(v.as.i, to));
		return v;
	case TY_FLOAT: {
		double d;
		if (v.kind == V_FLOAT)
			d = v.as.f;
		else if (v.kind == V_BOOL)
			d = v.as.b;
		else if (from->kind == TY_INT && !from->is_signed)
			d = (double)(uint64_t)v.as.i;
		else
			d = (double)v.as.i;
		return v_float(to->bits == 32 ? (double)(float)d : d);
	}
	default:
		return v;
	}
}

static Value arith(BinaryOp op, Value l, Value r, const Type *t, SrcLoc loc)
{
	if (t->kind == TY_FLOAT) {
		double x = l.as.f, y = r.as.f, z;
		switch (op) {
		case BIN_ADD: z = x + y; break;
		case BIN_SUB: z = x - y; break;
		case BIN_MUL: z = x * y; break;
		case BIN_DIV: z = x / y; break;
		default: internal_error(loc, "bad float operator");
		}
		return v_float(t->bits == 32 ? (double)(float)z : z);
	}
	int64_t x = l.kind == V_BOOL ? l.as.b : l.as.i;
	int64_t y = r.kind == V_BOOL ? r.as.b : r.as.i;
	uint64_t ux = (uint64_t)x, uy = (uint64_t)y;
	int bits = t->kind == TY_BOOL ? 8 : t->bits;
	bool sgn = t->kind == TY_INT && t->is_signed;
	const char *f = file_of(loc);
	int64_t z;
	switch (op) {
	case BIN_ADD: z = (int64_t)(ux + uy); break;
	case BIN_SUB: z = (int64_t)(ux - uy); break;
	case BIN_MUL: z = (int64_t)(ux * uy); break;
	case BIN_DIV:
	case BIN_MOD:
		set_panic_loc(loc);
		rt_check_div(x, y, bits, sgn, op == BIN_MOD, f, loc.line, loc.col);
		if (!sgn)
			z = (int64_t)(op == BIN_DIV ? ux / uy : ux % uy);
		else if (y == -1)
			z = op == BIN_DIV ? (int64_t)(0 - ux) : 0;
		else
			z = op == BIN_DIV ? x / y : x % y;
		break;
	case BIN_BIT_AND: z = x & y; break;
	case BIN_BIT_OR:  z = x | y; break;
	case BIN_BIT_XOR: z = x ^ y; break;
	case BIN_SHL:
		set_panic_loc(loc);
		rt_check_shift(y, bits, f, loc.line, loc.col);
		z = (int64_t)(ux << y);
		break;
	case BIN_SHR:
		set_panic_loc(loc);
		rt_check_shift(y, bits, f, loc.line, loc.col);
		if (sgn)
			z = x < 0 ? ~(~x >> y) : x >> y;
		else
			z = (int64_t)(ux >> y);
		break;
	default:
		internal_error(loc, "bad integer operator");
	}
	return v_int(norm(z, t));
}

static int compare(const Value *a, const Value *b, const Type *t)
{
	if (t->kind == TY_FLOAT)
		return (a->as.f > b->as.f) - (a->as.f < b->as.f);
	if (t->kind == TY_BOOL)
		return (a->as.b > b->as.b) - (a->as.b < b->as.b);
	if (t->kind == TY_INT && !t->is_signed) {
		uint64_t x = (uint64_t)a->as.i, y = (uint64_t)b->as.i;
		return (x > y) - (x < y);
	}
	return (a->as.i > b->as.i) - (a->as.i < b->as.i);
}

static Value eval_binary(Interp *I, Expr *e)
{
	BinaryOp op = e->as.binary.op;
	Expr *lhs = e->as.binary.lhs, *rhs = e->as.binary.rhs;

	if (op == BIN_LOG_AND || op == BIN_LOG_OR) {
		bool l = eval(I, lhs).as.b;
		if (op == BIN_LOG_AND ? !l : l)
			return v_bool(l);
		return v_bool(eval(I, rhs).as.b);
	}
	if (lhs->ty->kind == TY_ADDRESS) {
		Value l = eval(I, lhs), r = eval(I, rhs);
		if (ast_binop_is_comparison(op)) {
			int64_t x = address_number(l.as.ad, lhs->ty->inner), y = address_number(r.as.ad, lhs->ty->inner);
			int c = x < y ? -1 : x > y;
			switch (op) {
			case BIN_EQ: return v_bool(c == 0);
			case BIN_NE: return v_bool(c != 0);
			case BIN_LT: return v_bool(c < 0);
			case BIN_LE: return v_bool(c <= 0);
			case BIN_GT: return v_bool(c > 0);
			default:     return v_bool(c >= 0);
			}
		}
		/* A + n, A - n */
		AddrVal *a = l.as.ad;
		int64_t n = op == BIN_SUB ? -r.as.i : r.as.i;
		if (address_is_none(a))
			PANIC(e->loc, "addresses.none can't be stepped");
		return address_along(a, a->idx + n);
	}
	if (ast_binop_is_comparison(op)) {
		Value *a = deref_all(eval_read(I, lhs));
		Value av = *a;   /* numbers: keep the value read before the right side runs */
		if (a->kind == V_INT || a->kind == V_FLOAT || a->kind == V_BOOL)
			a = &av;
		Value *b = deref_all(eval_read(I, rhs));
		Type *t = lhs->ty;
		if (a->kind == V_STR) {
			bool eq = a->as.s->len == b->as.s->len && memcmp(a->as.s->data, b->as.s->data, (size_t)a->as.s->len) == 0;
			return v_bool(op == BIN_EQ ? eq : !eq);
		}
		if (t->kind == TY_FLOAT && (isnan(a->as.f) || isnan(b->as.f)))
			return v_bool(op == BIN_NE);
		int c = compare(a, b, t);
		switch (op) {
		case BIN_EQ: return v_bool(c == 0);
		case BIN_NE: return v_bool(c != 0);
		case BIN_LT: return v_bool(c < 0);
		case BIN_LE: return v_bool(c <= 0);
		case BIN_GT: return v_bool(c > 0);
		default:     return v_bool(c >= 0);
		}
	}
	Value l = eval(I, lhs);
	Value r = eval(I, rhs);
	if (op == BIN_MOD && e->ty->kind == TY_FLOAT)
		return v_float(fmod(l.as.f, r.as.f));
	return arith(op, l, r, e->ty, e->loc);
}

/* ---- strings: s[i], s.length, s.copy, a + b ---- */

static Str *str_of(Value *v)
{
	v = deref_all(v);
	return v->kind == V_STR ? v->as.s : &empty_str;
}

/* A str or char operand of +, as bytes. `buf` holds a char. */
static void str_operand(Interp *I, Expr *a, const char **data, int64_t *len, char *buf)
{
	if (a->ty->kind == TY_STR) {
		Str *s = str_of(eval_read(I, a));
		*data = s->data;
		*len = s->len;
	} else {
		buf[0] = (char)eval(I, a).as.i;
		*data = buf;
		*len = 1;
	}
}

/* n.text: as print shows it, into buf (room for 48) */
static int number_text(Interp *I, Expr *a, char *buf)
{
	Type *t = a->ty;
	Value v = eval(I, a);
	int what = t->kind == TY_BOOL ? RT_TEXT_BOOL : t->kind == TY_FLOAT ? (t->bits == 32 ? RT_TEXT_FLOAT : RT_TEXT_DOUBLE)
		   : t->is_char ? RT_TEXT_CHAR : t->is_signed ? RT_TEXT_INT : RT_TEXT_UINT;
	return rt_format_text(buf, what, v.kind == V_BOOL ? v.as.b : v.as.i, v.kind == V_FLOAT ? v.as.f : 0.0);
}

/* a number worked out with nothing that could change anything: literals,
 * variables, and arithmetic of them */
static bool calm_number(const Expr *e)
{
	switch (e->kind) {
	case EXPR_INT: case EXPR_FLOAT: case EXPR_BOOL: case EXPR_CHAR: case EXPR_IDENT: case EXPR_SELF:
		return true;
	case EXPR_UNARY:  return calm_number(e->as.unary.operand);
	case EXPR_CAST:   return calm_number(e->as.cast.value);
	case EXPR_BINARY: return calm_number(e->as.binary.lhs) && calm_number(e->as.binary.rhs);
	default:          return false;
	}
}

/* a piece of a join that can't change what came before it */
static bool calm_piece(const Expr *e)
{
	if (e->kind == EXPR_STRING || e->kind == EXPR_IDENT || e->kind == EXPR_SELF)
		return true;
	if (e->ty->kind != TY_STR)
		return calm_number(e);   /* a char */
	return e->kind == EXPR_CALL && e->as.call.builtin == BUILTIN_TEXT && calm_number(e->as.call.args.items[0]);
}

#define JOIN_MOST 32

static int join_pieces(Expr *e, Expr **pieces, int n)
{
	if (n < 0)
		return n;
	if (e->kind == EXPR_CALL && e->as.call.builtin == BUILTIN_STR_CONCAT) {
		n = join_pieces(e->as.call.args.items[0], pieces, n);
		return join_pieces(e->as.call.args.items[1], pieces, n);
	}
	if (n == JOIN_MOST)
		return -1;
	pieces[n] = e;
	return n + 1;
}

/* a + b + c ... made in one go, as compiled code does: one string, not one
 * for each +, and numbers' text written straight into it. Only when every
 * piece after the first can't change anything (the pieces are read, then
 * copied): otherwise false, and they're joined a + at a time. */
static bool concat_at_once(Interp *I, Expr *e, Value *out)
{
	Expr *pieces[JOIN_MOST];
	int n = join_pieces(e, pieces, 0);
	bool numbers = false;
	for (int i = 0; i < n && n > 0; i++)
		numbers |= pieces[i]->kind == EXPR_CALL && pieces[i]->as.call.builtin == BUILTIN_TEXT;
	if (n < 3 && !(n == 2 && numbers))
		return false;   /* (a + b of strings: the plain way is as quick) */
	for (int i = 1; i < n; i++)
		if (!calm_piece(pieces[i]))
			return false;
	const char *data[JOIN_MOST];
	int64_t len[JOIN_MOST], total = 0;
	char bufs[JOIN_MOST][48];
	for (int i = 0; i < n; i++) {
		Expr *p = pieces[i];
		if (p->kind == EXPR_CALL && p->as.call.builtin == BUILTIN_TEXT) {
			len[i] = number_text(I, p->as.call.args.items[0], bufs[i]);
			data[i] = bufs[i];
		} else {
			str_operand(I, p, &data[i], &len[i], bufs[i]);
		}
		total += len[i];
	}
	Str *r = str_block(sizeof *r + (size_t)total + 1);
	r->len = total;
	r->cap = total + 1;
	r->is_static = false;
	r->data = (char *)(r + 1);
	int64_t at = 0;
	for (int i = 0; i < n; i++) {
		memcpy(r->data + at, data[i], (size_t)len[i]);
		at += len[i];
	}
	r->data[total] = '\0';
	*out = (Value){ .kind = V_STR, .as.s = r };
	return true;
}

static Value str_concat(const char *a, int64_t na, const char *b, int64_t nb)
{
	Str *r = str_block(sizeof *r + (size_t)(na + nb) + 1);
	r->len = na + nb;
	r->cap = na + nb + 1;
	r->is_static = false;
	r->data = (char *)(r + 1);
	memcpy(r->data, a, (size_t)na);
	memcpy(r->data + na, b, (size_t)nb);
	r->data[na + nb] = '\0';
	return (Value){ .kind = V_STR, .as.s = r };
}

static int64_t str_index(Str *s, int64_t i, SrcLoc l)
{
	if (i < 0 || i >= s->len) {
		set_panic_loc(l);
		rt_str_bounds(i, s->len, file_of(l), l.line, l.col);
	}
	return i;
}

/* files.* and directories.*: the runtime does the work */
static Value eval_lib_builtin(Interp *I, Expr *e)
{
	ExprVec *args = &e->as.call.args;
	const char *a = NULL, *b = NULL;
	int64_t an = 0, bn = 0;
	char ca[1], cb[1];
	if (args->len > 0)
		str_operand(I, args->items[0], &a, &an, ca);
	if (args->len > 1)
		str_operand(I, args->items[1], &b, &bn, cb);
	set_panic_loc(e->loc);
	const char *f = file_of(e->loc);
	int line = e->loc.line, col = e->loc.col;
	int64_t n;
	char *text;
	rt_text *list;
	switch (e->as.call.builtin) {
	case BUILTIN_FILES_READ:
		text = rt_files_read(a, an, &n, f, line, col);
		goto give_text;
	case BUILTIN_DIRS_CURRENT:
		text = rt_dirs_current(&n);
		goto give_text;
	case BUILTIN_DIRS_HOME:
		text = rt_dirs_home(&n);
		goto give_text;
	case BUILTIN_DIRS_SAVE_FOLDER:
		text = rt_dirs_save_folder(a, an, &n, f, line, col);
		goto give_text;
	case BUILTIN_SHELL_RUN:
		text = rt_shell_run(a, an, &n, f, line, col);
		goto give_text;
	case BUILTIN_SHELL_GET:
		text = rt_shell_get(a, an, &n, f, line, col);
		goto give_text;
	case BUILTIN_SHELL_STATUS: return v_int(rt_shell_status(a, an, f, line, col));
	case BUILTIN_SHELL_HAS:    return v_bool(rt_shell_has(a, an));
	case BUILTIN_SHELL_SET:    rt_shell_set(a, an, b, bn, f, line, col); break;
	case BUILTIN_SHELL_UNSET:  rt_shell_unset(a, an, f, line, col); break;
	case BUILTIN_FILES_LINES:
		list = rt_files_lines(a, an, &n, f, line, col);
		goto give_list;
	case BUILTIN_DIRS_LIST:
	case BUILTIN_DIRS_ALL_FILES:
		list = rt_dirs_list(a, an, e->as.call.builtin == BUILTIN_DIRS_ALL_FILES, &n, f, line, col);
		goto give_list;
	case BUILTIN_FILES_WRITE:
	case BUILTIN_FILES_APPEND:
		rt_files_write(a, an, b, bn, e->as.call.builtin == BUILTIN_FILES_APPEND, f, line, col);
		break;
	case BUILTIN_FILES_EXISTS:  return v_bool(rt_files_exists(a, an));
	case BUILTIN_DIRS_EXISTS:   return v_bool(rt_dirs_exists(a, an));
	case BUILTIN_FILES_SIZE:    return v_int(rt_files_size(a, an, f, line, col));
	case BUILTIN_FILES_COPY:    rt_files_copy(a, an, b, bn, f, line, col); break;
	case BUILTIN_FILES_RENAME:  rt_files_rename(a, an, b, bn, f, line, col); break;
	case BUILTIN_FILES_DELETE:  rt_files_delete(a, an, f, line, col); break;
	case BUILTIN_DIRS_MAKE:     rt_dirs_make(a, an, f, line, col); break;
	case BUILTIN_DIRS_REMOVE:   rt_dirs_remove(a, an, f, line, col); break;
	case BUILTIN_DIRS_RENAME:   rt_dirs_rename(a, an, b, bn, f, line, col); break;
	case BUILTIN_DIRS_CHANGE_TO: rt_dirs_change_to(a, an, f, line, col); break;
	case BUILTIN_FILES_OPEN:
	case BUILTIN_FILES_OPEN_TO_WRITE:
	case BUILTIN_FILES_OPEN_TO_ADD:
	case BUILTIN_FILES_OPEN_TO_CHANGE: {
		int mode = e->as.call.builtin == BUILTIN_FILES_OPEN ? RT_OPEN_READ :
			   e->as.call.builtin == BUILTIN_FILES_OPEN_TO_WRITE ? RT_OPEN_WRITE :
			   e->as.call.builtin == BUILTIN_FILES_OPEN_TO_ADD ? RT_OPEN_ADD : RT_OPEN_CHANGE;
		return (Value){ .kind = V_FILE, .as.file = rt_file_open(a, an, mode, f, line, col) };
	}
	default: break;
	}
	return v_void();

give_text: {
	Value v = { .kind = V_STR, .as.s = str_new(text, n) };
	free(text);
	return v;
}
give_list: {
	Value v = zero_value(e->ty);
	list_reserve(v.as.list, n);
	for (int64_t i = 0; i < n; i++)
		v.as.list->items[v.as.list->len++] = (Value){ .kind = V_STR, .as.s = str_new(list[i].data, list[i].len) };
	rt_texts_free(list, n);
	return v;
}
}

static Value eval_str_builtin(Interp *I, Expr *e)
{
	ExprVec *args = &e->as.call.args;
	switch (e->as.call.builtin) {
	case BUILTIN_STR_LEN:
		return v_int(str_of(eval_read(I, args->items[0]))->len);
	case BUILTIN_SLICE_LEN:
		return v_int(deref_all(eval_read(I, args->items[0]))->as.arr->len);
	case BUILTIN_STR_COPY: {
		Str *s = str_of(eval_read(I, args->items[0]));
		return (Value){ .kind = V_STR, .as.s = str_new(s->data, s->len) };
	}
	case BUILTIN_STR_CONCAT: {
		Value joined;
		if (concat_at_once(I, e, &joined))
			return joined;
		const char *a, *b;
		int64_t na, nb;
		char ca[1], cb[1];
		str_operand(I, args->items[0], &a, &na, ca);
		str_operand(I, args->items[1], &b, &nb, cb);
		return str_concat(a, na, b, nb);
	}
	case BUILTIN_TEXT: {
		char buf[48];
		int n = number_text(I, args->items[0], buf);
		return (Value){ .kind = V_STR, .as.s = str_new(buf, n) };
	}
	case BUILTIN_STR_GET: {
		Str *s = str_of(eval_read(I, args->items[0]));
		int64_t i = str_index(s, eval(I, args->items[1]).as.i, e->loc);
		return v_int((int8_t)s->data[i]);
	}
	case BUILTIN_INPUT: {
		Str *p = str_of(eval_read(I, args->items[0]));
		rt_print_str(p->data, p->len);
	}
	/* fall through */
	case BUILTIN_READ_LINE: {
		int64_t n;
		set_panic_loc(e->loc);
		char *line = rt_read_line(&n, file_of(e->loc), e->loc.line, e->loc.col);
		Value v = { .kind = V_STR, .as.s = str_new(line, n) };
		free(line);
		return v;
	}
	case BUILTIN_END_OF_INPUT:
		return v_bool(rt_input_ended);
	case BUILTIN_ABS: {
		Value v = eval(I, args->items[0]);
		if (v.kind == V_FLOAT)
			return v_float(fabs(v.as.f));
		if (!e->ty->is_signed || v.as.i >= 0)
			return v;
		return v_int(norm((int64_t)(0 - (uint64_t)v.as.i), e->ty));
	}
	case BUILTIN_CLOSE_TO: {
		double a = eval(I, args->items[0]).as.f, b = eval(I, args->items[1]).as.f;
		double limit = args->len > 2 ? eval(I, args->items[2]).as.f : 0;
		return v_bool(rt_close_to(a, b, limit, (e->as.call.close_floats ? 1 : 0) | (args->len > 2 ? 2 : 0)));
	}
	case BUILTIN_RANDOM: {
		Value a = eval(I, args->items[0]), b = eval(I, args->items[1]);
		if (e->ty->kind == TY_FLOAT)
			return v_float(rt_random_double(a.as.f, b.as.f));
		return v_int(rt_random_int(a.as.i, b.as.i));
	}
	case BUILTIN_RANDOM_SEED:
		rt_random_seed(eval(I, args->items[0]).as.i);
		return v_void();
	case BUILTIN_EXIT: {
		int st = args->len ? (int)eval(I, args->items[0]).as.i : 0;
		rt_flush_files();
		fflush(stdout);
		fflush(stderr);
		exit(st & 0xFF);
	}
	case BUILTIN_TEXT_DECIMALS: {
		Value v = eval(I, args->items[0]);
		int64_t places = eval(I, args->items[1]).as.i;
		char buf[400];
		set_panic_loc(e->loc);
		int n = v.kind == V_FLOAT ? rt_text_decimals(buf, sizeof buf, v.as.f, places)
					  : rt_text_decimals_int(buf, sizeof buf, v.as.i, args->items[0]->ty->is_signed, places);
		return (Value){ .kind = V_STR, .as.s = str_new(buf, n) };
	}
	case BUILTIN_RECOVERED: {
		const char *m = rt_recovered_message();
		return (Value){ .kind = V_STR, .as.s = str_new(m, (int64_t)strlen(m)) };
	}
	case BUILTIN_READ_CHAR:
		set_panic_loc(e->loc);
		return v_int((int8_t)rt_read_char(file_of(e->loc), e->loc.line, e->loc.col));
	case BUILTIN_READ_KEY: {
		char buf[16];
		set_panic_loc(e->loc);
		int64_t n = rt_read_key(buf, file_of(e->loc), e->loc.line, e->loc.col);
		return (Value){ .kind = V_STR, .as.s = str_new(buf, n) };
	}
	case BUILTIN_FILES_READ: case BUILTIN_FILES_LINES: case BUILTIN_FILES_WRITE: case BUILTIN_FILES_APPEND:
	case BUILTIN_FILES_EXISTS: case BUILTIN_FILES_SIZE: case BUILTIN_FILES_COPY: case BUILTIN_FILES_RENAME:
	case BUILTIN_FILES_DELETE: case BUILTIN_DIRS_LIST: case BUILTIN_DIRS_ALL_FILES: case BUILTIN_DIRS_EXISTS:
	case BUILTIN_DIRS_MAKE: case BUILTIN_DIRS_REMOVE: case BUILTIN_DIRS_RENAME: case BUILTIN_DIRS_CURRENT:
	case BUILTIN_DIRS_CHANGE_TO: case BUILTIN_DIRS_HOME: case BUILTIN_FILES_OPEN: case BUILTIN_FILES_OPEN_TO_WRITE:
	case BUILTIN_FILES_OPEN_TO_ADD: case BUILTIN_FILES_OPEN_TO_CHANGE: case BUILTIN_SHELL_RUN: case BUILTIN_SHELL_STATUS:
	case BUILTIN_SHELL_GET: case BUILTIN_SHELL_HAS: case BUILTIN_SHELL_SET: case BUILTIN_SHELL_UNSET:
	case BUILTIN_DIRS_SAVE_FOLDER:
		return eval_lib_builtin(I, e);
	case BUILTIN_STR_FIND:
	case BUILTIN_STR_CONTAINS:
	case BUILTIN_STR_STARTS_WITH:
	case BUILTIN_STR_ENDS_WITH:
	case BUILTIN_STR_COMPARE: {
		const char *a, *b;
		int64_t na, nb;
		char ca[1], cb[1];
		str_operand(I, args->items[0], &a, &na, ca);
		str_operand(I, args->items[1], &b, &nb, cb);
		switch (e->as.call.builtin) {
		case BUILTIN_STR_FIND:        return v_int(rt_str_find(a, na, b, nb));
		case BUILTIN_STR_CONTAINS:    return v_bool(rt_str_test(a, na, b, nb, RT_TEST_CONTAINS));
		case BUILTIN_STR_STARTS_WITH: return v_bool(rt_str_test(a, na, b, nb, RT_TEST_STARTS));
		case BUILTIN_STR_ENDS_WITH:   return v_bool(rt_str_test(a, na, b, nb, RT_TEST_ENDS));
		default:                      return v_int(rt_str_compare(a, na, b, nb));
		}
	}
	case BUILTIN_STR_TRIM: {
		Str *s = str_of(eval_read(I, args->items[0]));
		int64_t a, n;
		rt_str_trim(s->data, s->len, &a, &n);
		return (Value){ .kind = V_STR, .as.s = str_new(s->data + a, n) };
	}
	case BUILTIN_STR_UPPER:
	case BUILTIN_STR_LOWER: {
		Str *s = str_of(eval_read(I, args->items[0]));
		bool upper = e->as.call.builtin == BUILTIN_STR_UPPER;
		bool ascii = true;
		for (int64_t i = 0; i < s->len && ascii; i++)
			ascii = (unsigned char)s->data[i] < 0x80;
		if (!ascii) {   /* letters beyond A to Z: rt_str_case knows them */
			int64_t n;
			char *p = rt_str_case(s->data, s->len, upper, &n);
			Str *r = str_new(p, n);
			free(p);
			return (Value){ .kind = V_STR, .as.s = r };
		}
		/* (A to Z alone: straight into the new string) */
		Str *r = str_new(s->data, s->len);
		for (int64_t i = 0; i < r->len; i++) {
			char c = r->data[i];
			if (upper && c >= 'a' && c <= 'z')
				r->data[i] = (char)(c - 32);
			else if (!upper && c >= 'A' && c <= 'Z')
				r->data[i] = (char)(c + 32);
		}
		return (Value){ .kind = V_STR, .as.s = r };
	}
	case BUILTIN_STR_REPLACE: {
		/* (straight into the new string, as rt_str_replace makes it: every
		 * `from` replaced by `to`, left to right, not overlapping; an empty
		 * `from` changes nothing) */
		Str *s = str_of(eval_read(I, args->items[0]));
		const char *f, *t;
		int64_t nf, nt;
		char cf[1], ct[1];
		str_operand(I, args->items[1], &f, &nf, cf);
		str_operand(I, args->items[2], &t, &nt, ct);
		int64_t count = 0, at = 0, k;
		if (nf > 0)
			for (; (k = rt_str_find(s->data + at, s->len - at, f, nf)) >= 0; at += k + nf)
				count++;
		if (count == 0)
			return (Value){ .kind = V_STR, .as.s = str_new(s->data, s->len) };
		int64_t m = s->len + count * (nt - nf);
		Str *r = str_block(sizeof *r + (size_t)m + 1);
		r->len = m;
		r->cap = m + 1;
		r->is_static = false;
		r->data = (char *)(r + 1);
		int64_t o = 0;
		at = 0;
		while ((k = rt_str_find(s->data + at, s->len - at, f, nf)) >= 0) {
			memcpy(r->data + o, s->data + at, (size_t)k);
			o += k;
			memcpy(r->data + o, t, (size_t)nt);
			o += nt;
			at += k + nf;
		}
		memcpy(r->data + o, s->data + at, (size_t)(s->len - at));
		r->data[m] = '\0';
		return (Value){ .kind = V_STR, .as.s = r };
	}
	case BUILTIN_STR_PAD_LEFT:
	case BUILTIN_STR_PAD_RIGHT:
	case BUILTIN_STR_CENTERED:
	case BUILTIN_STR_REPEAT: {
		Str *s = str_of(eval_read(I, args->items[0]));
		char *p;
		int64_t n = s->len;
		if (e->as.call.builtin == BUILTIN_STR_REPLACE) {
			const char *f, *t;
			int64_t nf, nt;
			char cf[1], ct[1];
			str_operand(I, args->items[1], &f, &nf, cf);
			str_operand(I, args->items[2], &t, &nt, ct);
			p = rt_str_replace(s->data, s->len, f, nf, t, nt, &n);
		} else if (e->as.call.builtin >= BUILTIN_STR_PAD_LEFT && e->as.call.builtin <= BUILTIN_STR_CENTERED) {
			int64_t width = eval(I, args->items[1]).as.i;
			p = rt_str_pad(s->data, s->len, width, (int)(e->as.call.builtin - BUILTIN_STR_PAD_LEFT), &n);
		} else if (e->as.call.builtin == BUILTIN_STR_REPEAT) {
			int64_t times = eval(I, args->items[1]).as.i;
			set_panic_loc(e->loc);
			p = rt_str_repeat(s->data, s->len, times, &n, file_of(e->loc), e->loc.line, e->loc.col);
		} else {
			p = rt_str_case(s->data, s->len, e->as.call.builtin == BUILTIN_STR_UPPER, &n);
		}
		Value v = { .kind = V_STR, .as.s = str_new(p, n) };
		free(p);
		return v;
	}
	case BUILTIN_STR_SLICE: {
		Str *s = str_of(eval_read(I, args->items[0]));
		int64_t from = eval(I, args->items[1]).as.i, to = eval(I, args->items[2]).as.i;
		int flags = (int)args->items[3]->as.int_lit.value;
		int64_t a, n;
		set_panic_loc(e->loc);
		rt_str_slice(s->len, from, to, flags, &a, &n, file_of(e->loc), e->loc.line, e->loc.col);
		return (Value){ .kind = V_STR, .as.s = str_new(s->data + a, n) };
	}
	case BUILTIN_LIST_LEN:
		return v_int(list_of(eval_read(I, args->items[0]))->len);
	case BUILTIN_LIST_ADD:
	case BUILTIN_LIST_INSERT: {
		List *l = list_of(eval_read(I, args->items[0]));
		int64_t at = l->len;
		if (e->as.call.builtin == BUILTIN_LIST_INSERT) {
			at = eval(I, args->items[1]).as.i;
			list_index_check(at, l->len, true, e->loc);
		}
		Value x = eval(I, args->items[args->len - 1]);
		list_reserve(l, 1);
		memmove(&l->items[at + 1], &l->items[at], sizeof(Value) * (size_t)(l->len - at));
		l->items[at] = x;
		l->len++;
		list_resized(l, e->as.call.builtin == BUILTIN_LIST_INSERT ? "an item was inserted" : "an item was added");
		return v_void();
	}
	case BUILTIN_LIST_REMOVE_AT: {
		List *l = list_of(eval_read(I, args->items[0]));
		int64_t at = eval(I, args->items[1]).as.i;
		list_index_check(at, l->len, false, e->loc);
		Value gone = l->items[at];
		memmove(&l->items[at], &l->items[at + 1], sizeof(Value) * (size_t)(l->len - at - 1));
		l->len--;
		list_resized(l, "an item was removed");
		value_drop(I, &gone);
		return v_void();
	}
	case BUILTIN_LIST_TAKE_LAST: {
		List *l = list_of(eval_read(I, args->items[0]));
		if (l->len == 0) {
			set_panic_loc(e->loc);
			rt_panic(file_of(e->loc), e->loc.line, e->loc.col, "take_last on an empty list");
		}
		list_resized(l, "an item was taken off the end");
		return l->items[--l->len];
	}
	case BUILTIN_LIST_CLEAR: {
		List *l = list_of(eval_read(I, args->items[0]));
		int64_t n = l->len;
		l->len = 0;
		list_resized(l, "it was cleared");
		for (int64_t i = 0; i < n; i++)
			value_drop(I, &l->items[i]);
		return v_void();
	}
	case BUILTIN_LIST_CONTAINS:
	case BUILTIN_LIST_FIND: {
		List *l = list_of(eval_read(I, args->items[0]));
		Value *x = deref_all(eval_read(I, args->items[1]));
		int64_t at = -1;
		for (int64_t i = 0; i < l->len && at < 0; i++)
			if (same_value(&l->items[i], x))
				at = i;
		return e->as.call.builtin == BUILTIN_LIST_FIND ? v_int(at) : v_bool(at >= 0);
	}
	case BUILTIN_LIST_COPY:
		return deep_copy(deref_all(eval_read(I, args->items[0])), e->loc);
	case BUILTIN_LIST_SLICE: {
		List *l = list_of(eval_read(I, args->items[0]));
		int64_t from = eval(I, args->items[1]).as.i, to = eval(I, args->items[2]).as.i;
		int flags = (int)args->items[3]->as.int_lit.value;
		int64_t a, n;
		set_panic_loc(e->loc);
		rt_list_slice(l->len, from, to, flags, &a, &n, file_of(e->loc), e->loc.line, e->loc.col);
		Value v = zero_value(e->ty);
		list_reserve(v.as.list, n);
		for (int64_t i = 0; i < n; i++)
			v.as.list->items[i] = deep_copy(&l->items[a + i], e->loc);
		v.as.list->len = n;
		return v;
	}
	case BUILTIN_LOOKUP_LEN:
		return v_int(lookup_of(eval_read(I, args->items[0]))->len);
	case BUILTIN_LOOKUP_CONTAINS: {
		Lookup *k = lookup_of(eval_read(I, args->items[0]));
		return v_bool(lk_find(k, deref_all(eval_read(I, args->items[1]))) >= 0);
	}
	case BUILTIN_LOOKUP_REMOVE: {
		Lookup *k = lookup_of(eval_read(I, args->items[0]));
		Value *key = deref_all(eval_read(I, args->items[1]));
		int64_t pos = lk_find(k, key);
		if (pos < 0)
			lk_missing(key, args->items[0]->ty->key, e->loc);
		lk_kill(I, k, pos);
		return v_void();
	}
	case BUILTIN_LOOKUP_CLEAR: {
		Lookup *k = lookup_of(eval_read(I, args->items[0]));
		for (int64_t p = 0; p < k->n; p++)
			if (!k->dead[p]) {
				value_drop(I, &k->keys[p]);
				value_drop(I, &k->vals[p]);
			}
		k->n = k->len = 0;
		if (k->index)
			memset(k->index, 0, sizeof(int32_t) * (size_t)k->icap);
		return v_void();
	}
	case BUILTIN_LOOKUP_COPY:
		return lk_copy(lookup_of(eval_read(I, args->items[0])), e->loc);
	case BUILTIN_LOOKUP_KEYS:
	case BUILTIN_LOOKUP_VALUES: {
		Lookup *k = lookup_of(eval_read(I, args->items[0]));
		Value v = zero_value(e->ty);
		list_reserve(v.as.list, k->len);
		for (int64_t p = 0; p < k->n; p++)
			if (!k->dead[p])
				v.as.list->items[v.as.list->len++] =
					deep_copy(e->as.call.builtin == BUILTIN_LOOKUP_KEYS ? &k->keys[p] : &k->vals[p], e->loc);
		return v;
	}
	case BUILTIN_LOOKUP_NEXT: {
		Lookup *k = lookup_of(eval_read(I, args->items[0]));
		int64_t pos = eval(I, args->items[1]).as.i;
		while (pos < k->n && k->dead[pos])
			pos++;
		return v_int(pos < k->n ? pos : -1);
	}
	case BUILTIN_LIST_SORT: {
		List *l = list_of(eval_read(I, args->items[0]));
		const Type *el = args->items[0]->ty->inner;
		int (*cmp)(const void *, const void *) =
			el->kind == TY_STR ? cmp_value_str : el->kind == TY_FLOAT ? cmp_value_float :
			el->kind == TY_INT && el->is_char ? cmp_value_char :
			el->kind == TY_INT && !el->is_signed ? cmp_value_unsigned : cmp_value_signed;
		if (l->len > 1)
			qsort(l->items, (size_t)l->len, sizeof(Value), cmp);
		return v_void();
	}
	case BUILTIN_LIST_SORT_BY:
	case BUILTIN_LIST_SORT_WITH:
		list_sort_by(I, list_of(eval_read(I, args->items[0])), args->items[1], e->as.call.builtin == BUILTIN_LIST_SORT_WITH, e->loc);
		return v_void();
	case BUILTIN_LIST_REVERSE: {
		List *l = list_of(eval_read(I, args->items[0]));
		for (int64_t a = 0, b = l->len - 1; a < b; a++, b--) {
			Value t = l->items[a];
			l->items[a] = l->items[b];
			l->items[b] = t;
		}
		return v_void();
	}
	case BUILTIN_LIST_JOIN: {
		List *l = list_of(eval_read(I, args->items[0]));
		const char *sep;
		int64_t ns;
		char cs[1];
		str_operand(I, args->items[1], &sep, &ns, cs);
		int64_t total = 0;
		for (int64_t i = 0; i < l->len; i++)
			total += l->items[i].as.s->len + (i ? ns : 0);
		Str *r = rt_alloc(sizeof *r + (size_t)total + 1);
		r->len = total;
		r->cap = total + 1;
		r->is_static = false;
		r->data = (char *)(r + 1);
		char *o = r->data;
		for (int64_t i = 0; i < l->len; i++) {
			if (i) {
				memcpy(o, sep, (size_t)ns);
				o += ns;
			}
			memcpy(o, l->items[i].as.s->data, (size_t)l->items[i].as.s->len);
			o += l->items[i].as.s->len;
		}
		*o = '\0';
		return (Value){ .kind = V_STR, .as.s = r };
	}
	case BUILTIN_STR_SPLIT: {
		const char *a, *b;
		int64_t na, nb;
		char ca[1], cb[1];
		str_operand(I, args->items[0], &a, &na, ca);
		str_operand(I, args->items[1], &b, &nb, cb);
		Value v = zero_value(e->ty);
		List *l = v.as.list;
		int64_t at = 0;
		for (;;) {
			int64_t k = nb ? rt_str_find(a + at, na - at, b, nb) : -1;
			list_reserve(l, 1);
			if (k < 0) {
				l->items[l->len++] = (Value){ .kind = V_STR, .as.s = str_new(a + at, na - at) };
				break;
			}
			l->items[l->len++] = (Value){ .kind = V_STR, .as.s = str_new(a + at, k) };
			at += k + nb;
		}
		return v;
	}
	case BUILTIN_TO_FILE: {
		Value *f = deref_all(eval_read(I, args->items[0]));
		set_panic_loc(e->loc);
		rt_out_push_file(f->kind == V_FILE ? f->as.file : NULL, file_of(e->loc), e->loc.line, e->loc.col);
		Value v = eval(I, args->items[1]);
		rt_out_pop();
		return v;
	}
	case BUILTIN_FILE_HANDLE: {
		Value *f = deref_all(eval_read(I, args->items[0]));
		return v_int(f->kind == V_FILE ? (int64_t)(intptr_t)f->as.file : 0);
	}
	case BUILTIN_FILE_READ_LINE:
	case BUILTIN_FILE_AT_END: {
		Value *f = deref_all(eval_read(I, args->items[0]));
		rt_file *fp = f->kind == V_FILE ? f->as.file : NULL;
		set_panic_loc(e->loc);
		if (e->as.call.builtin == BUILTIN_FILE_AT_END)
			return v_bool(rt_file_at_end(fp, file_of(e->loc), e->loc.line, e->loc.col));
		int64_t n;
		char *text = rt_file_read_line(fp, &n, file_of(e->loc), e->loc.line, e->loc.col);
		Value v = { .kind = V_STR, .as.s = str_new(text, n) };
		free(text);
		return v;
	}
	case BUILTIN_ADDRESSES_OF: {
		Expr *x = args->items[0];
		if (x->kind == EXPR_AT)
			return address_of_at(I, x);
		char what[96];
		describe_place(I, x, what, sizeof what);
		Value r;
		if (x->kind == EXPR_INDEX && (x->as.index.base->ty->kind == TY_LIST ||
		    (x->as.index.base->ty->kind == TY_REF && x->as.index.base->ty->inner->kind == TY_LIST))) {
			Value *c = eval_place(I, x);   /* checks the index */
			List *l = deref_all(eval_read(I, x->as.index.base))->as.list;
			return list_address(l, c - l->items, e->ty->inner, what);
		}
		if (x->kind == EXPR_INDEX && x->as.index.base->ty->kind == TY_ARRAY) {
			Value *c = place_to_keep(I, x);   /* checks the index */
			Value *base = deref_all(eval_read(I, x->as.index.base));
			r = make_address(NULL, base->as.arr, c - base->as.arr->elems, NULL, e->ty->inner);
			r.as.ad->watch = watch_place(base->as.arr, what);
			return r;
		}
		Value *c = deref_all(place_to_keep(I, x));
		if (c->kind == V_ARRAY) {
			array_spread(c);
			r = make_address(NULL, c->as.arr, 0, NULL, e->ty->inner);   /* an array: its first element */
			r.as.ad->watch = watch_place(c->as.arr, what);
			return r;
		}
		r = make_address(c, NULL, 0, NULL, e->ty->inner);
		r.as.ad->watch = watch_place(c, what);
		return r;
	}
	case BUILTIN_ADDRESSES_OF_ITEMS: {
		char what[96];
		describe_place(I, args->items[0], what, sizeof what);
		List *l = deref_all(eval_place(I, args->items[0]))->as.list;
		char items[110];
		snprintf(items, sizeof items, "the items of %s", what);
		return list_address(l, 0, e->ty->inner, items);
	}
	case BUILTIN_ADDRESSES_OF_TEXT: {
		char what[96];
		describe_place(I, args->items[0], what, sizeof what);
		Value *sv = deref_all(eval_place(I, args->items[0]));
		if (sv->kind != V_STR)
			*sv = (Value){ .kind = V_STR, .as.s = &empty_str };
		if (sv->as.s->is_static)   /* changes go to the string's own copy, as for s[i] = c */
			sv->as.s = str_new(sv->as.s->data, sv->as.s->len);
		Str *st = sv->as.s;
		Value r = raw_address(st->data);
		char text[110];
		snprintf(text, sizeof text, "the text of %s", what);
		int w = watch_place(st, text);
		g_watches[w].lo = (unsigned char *)st->data;
		g_watches[w].hi = (unsigned char *)st->data + st->len + 1;
		r.as.ad->watch = w;
		return r;
	}
	case BUILTIN_ADDRESSES_AT:
		return raw_address((void *)(uintptr_t)eval(I, args->items[0]).as.i);
	case BUILTIN_ADDRESSES_ALLOCATE: {
		int64_t n = eval(I, args->items[0]).as.i;
		uint64_t es = type_size(e->ty->inner);
		if (n < 0)
			PANIC(e->loc, "can't allocate room for %lld values", (long long)n);
		unsigned char *p = calloc((size_t)n * es + GUARD_ZONE, 1);
		if (!p)
			PANIC(e->loc, "out of memory: can't allocate %lld values", (long long)n);
		int site = add_block(p, (uint64_t)n * es, e->loc);
		look_for_lost(I, site, p);
		return raw_address(p);
	}
	case BUILTIN_ADDRESSES_FREE:
	case BUILTIN_ADDRESSES_REALLOCATE: {
		bool re = e->as.call.builtin == BUILTIN_ADDRESSES_REALLOCATE;
		AddrVal *a = eval(I, args->items[0]).as.ad;
		int64_t n = re ? eval(I, args->items[1]).as.i : 0;
		if (address_is_none(a)) {
			if (!re)
				return v_void();   /* as C's free(NULL): nothing */
			unsigned char *p = calloc((size_t)n * type_size(e->ty->inner) + GUARD_ZONE, 1);
			add_block(p, (uint64_t)n * type_size(e->ty->inner), e->loc);
			return raw_address(p);
		}
		if (!a->raw || a->watch >= 0)
			PANIC(e->loc, "this address is of one of Cabby's own values, not memory from addresses.allocate");
		uint64_t es = args->items[0]->ty->inner ? type_size(args->items[0]->ty->inner) : 1;
		unsigned char *p = a->raw + a->idx * (int64_t)es;
		MemBlock *b = block_at(p);
		char w1[160];
		if (b && b->p != p)
			PANIC(e->loc, "this address is inside a block, not at its start (allocated %s)",
			      where_short(&b->made, w1, sizeof w1));
		if (b && !b->live)
			PANIC(e->loc, "this memory was already given back by %s %s", b->reallocated ? "addresses.reallocate"
			      : "addresses.free", where_str(&b->gone, false, w1, sizeof w1));
		if (!b) {
			/* memory from C: given back the C way */
			if (!re) {
				free(p);
				return v_void();
			}
			void *q = realloc(p, (size_t)(n > 0 ? n : 1) * type_size(e->ty->inner));
			if (!q)
				PANIC(e->loc, "out of memory: can't reallocate to %lld values", (long long)n);
			return raw_address(q);
		}
		b->live = false;
		b->reallocated = re;
		b->gone = where_of(e->loc);
		if (!re)
			return v_void();
		if (n < 0)
			PANIC(e->loc, "can't reallocate to room for %lld values", (long long)n);
		uint64_t size = (uint64_t)n * type_size(e->ty->inner);
		unsigned char *q = calloc(size + GUARD_ZONE, 1);
		if (!q)
			PANIC(e->loc, "out of memory: can't reallocate to %lld values", (long long)n);
		memcpy(q, b->p, size < b->size ? size : b->size);
		Where made = b->made;
		add_block_at(q, size, made, b->site);
		return raw_address(q);
	}
	case BUILTIN_EVENTS_RUN:
		cby_events_run();
		return v_void();
	case BUILTIN_EVENTS_STOP:
		cby_events_stop();
		return v_void();
	case BUILTIN_ADDRESSES_NONE:
		return raw_address(NULL);
	case BUILTIN_ADDRESS_NUMBER:
		return v_int(address_number(eval(I, args->items[0]).as.ad, args->items[0]->ty->inner));
	case BUILTIN_ADDRESS_IS_NONE:
		return v_bool(address_is_none(eval(I, args->items[0]).as.ad));
	case BUILTIN_LINK_IS_SET:
		return v_bool(cby_link_is_set(eval(I, args->items[0]).as.i));
	case BUILTIN_LINK_CLEAR:
		eval_place(I, args->items[0])->as.i = 0;
		return v_void();
	case BUILTIN_ADDRESS_DIFF: {
		AddrVal *a = eval(I, args->items[0]).as.ad, *b = eval(I, args->items[1]).as.ad;
		Type *el = args->items[0]->ty->inner;
		if (!address_is_none(a) && !address_is_none(b) && ((a->arr && a->arr == b->arr) || (a->list && a->list == b->list)))
			return v_int(a->idx - b->idx);
		if (!address_is_none(a) && !address_is_none(b) && a->cell && a->cell == b->cell)
			return v_int(a->idx - b->idx);
		if (!address_is_none(a) && !address_is_none(b) && a->raw && b->raw)
			return v_int((address_number(a, el) - address_number(b, el)) / (int64_t)type_size(el));
		PANIC(e->loc, "these two addresses aren't in the same array, so they can't be counted apart");
	}
	case BUILTIN_HANDLER_SET:
		*eval_place(I, args->items[0]) = eval(I, args->items[1]);
		return v_void();
	case BUILTIN_HANDLER_CLEAR:
		*eval_place(I, args->items[0]) = (Value){ .kind = V_HANDLER, .as.hv = NULL };
		return v_void();
	case BUILTIN_HANDLER_CALL:
	case BUILTIN_HANDLER_IS_SET: {
		Expr *h = args->items[0];
		Value v = h->kind == EXPR_FIELD ? *eval_place(I, h) : eval(I, h);
		HandlerVal *hv = v.kind == V_HANDLER ? v.as.hv : NULL;
		if (e->as.call.builtin == BUILTIN_HANDLER_IS_SET)
			return v_bool(hv && hv->fn);
		if (hv && hv->fn) {
			/* the object's method, as a window calls it */
			Value cell = { .kind = V_OBJECT, .as.obj = hv->obj };
			Value self = v_ref(&cell);
			int mark = I->ntemps;
			call_function(I, hv->fn, &self, NULL, e->loc);
			release_temps(I, mark);
		}
		return v_void();
	}
	case BUILTIN_TO_STDOUT:
	case BUILTIN_TO_STDERR: {
		rt_out_push(e->as.call.builtin == BUILTIN_TO_STDOUT ? RT_STDOUT : RT_STDERR);
		Value v = eval(I, args->items[0]);
		rt_out_pop();
		return v;
	}
	case BUILTIN_STR_IS_INT:
	case BUILTIN_STR_IS_LONG:
	case BUILTIN_STR_IS_NUMBER: {
		Str *s = str_of(eval_read(I, args->items[0]));
		int what = e->as.call.builtin == BUILTIN_STR_IS_INT ? RT_NUM_INT
			   : e->as.call.builtin == BUILTIN_STR_IS_LONG ? RT_NUM_LONG : RT_NUM_DOUBLE;
		return v_bool(rt_parse_number(s->data, s->len, what, NULL, NULL) == 1);
	}
	case BUILTIN_STR_TO_INT:
	case BUILTIN_STR_TO_LONG: {
		Str *s = str_of(eval_read(I, args->items[0]));
		set_panic_loc(e->loc);
		int what = e->as.call.builtin == BUILTIN_STR_TO_INT ? RT_NUM_INT : RT_NUM_LONG;
		return v_int(rt_text_to_int(s->data, s->len, what, file_of(e->loc), e->loc.line, e->loc.col));
	}
	case BUILTIN_STR_TO_DOUBLE: {
		Str *s = str_of(eval_read(I, args->items[0]));
		set_panic_loc(e->loc);
		return v_float(rt_text_to_double(s->data, s->len, file_of(e->loc), e->loc.line, e->loc.col));
	}
	default:
		internal_error(e->loc, "unknown builtin");
	}
}

/* Plain data (structs and arrays of numbers and the like) is changed where
 * it is, as in C, so an address of a field or element stays good. */
static void assign_in_place(Interp *I, Value *dst, Value src)
{
	if (dst->kind == V_OBJECT && src.kind == V_OBJECT && dst->as.obj->cls == src.as.obj->cls) {
		for (int i = 0; i < dst->as.obj->cls->field_count; i++)
			assign_in_place(I, &dst->as.obj->fields[i], src.as.obj->fields[i]);
		rt_free(src.as.obj);
		return;
	}
	if (dst->kind == V_ARRAY && src.kind == V_ARRAY && dst->as.arr->len == src.as.arr->len) {
		Array *d = dst->as.arr, *s = src.as.arr;
		if (d->raw && s->raw)
			memcpy(ARR_BYTES(d), ARR_BYTES(s), (size_t)(d->es * d->len));
		else if (d->raw)
			for (int64_t i = 0; i < d->len; i++)
				raw_put(d, i, &s->elems[i]);
		else
			for (int64_t i = 0; i < d->len; i++) {
				if (s->raw) {
					Value x = raw_get(s, i);
					assign_in_place(I, &d->elems[i], x);
				} else {
					assign_in_place(I, &d->elems[i], s->elems[i]);
				}
			}
		rt_free(s);
		return;
	}
	if (dst->kind != V_UNINIT)
		value_drop(I, dst);
	*dst = src;
}

static void exec_assign(Interp *I, Expr *e)
{
	Expr *target = e->as.assign.target;
	if (e->as.assign.compound && target->ty->kind == TY_STR) {
		/* s += t: the old text is dropped */
		const char *b;
		int64_t nb;
		char cb[1];
		str_operand(I, e->as.assign.value, &b, &nb, cb);
		Value *place = deref_all(eval_place(I, target));
		Str *old = str_of(place);
		if (nb == 0)
			return;
		int64_t need = old->len + nb + 1;
		bool alias = b >= old->data && b <= old->data + old->len;
		if (place->kind == V_STR && !old->is_static && !alias && old->data == (char *)(old + 1)) {
			if (old->cap < need) {
				/* regrow to double the size, so building a string a piece
				 * at a time stays in proportion to its length */
				int64_t cap = old->cap * 2 < need ? need : old->cap * 2;
				if (cap < 32)
					cap = 32;
				if (g_nlive_watches)
					places_gone(old, old + 1, "the string grew, so its characters moved");
				Str *grown = realloc(old, sizeof *old + (size_t)cap);
				if (!grown)
					rt_panic(NULL, 0, 0, "out of memory");
				grown->data = (char *)(grown + 1);
				grown->cap = cap;
				place->as.s = old = grown;
			}
			memcpy(old->data + old->len, b, (size_t)nb);
			old->len += nb;
			old->data[old->len] = '\0';
			return;
		}
		Value nv = str_concat(old->data, old->len, b, nb);
		value_drop(I, place);
		*place = nv;
		return;
	}
	Value val = eval(I, e->as.assign.value);   /* right-hand side first */
	if (target->kind == EXPR_INDEX && target->as.index.lookup_put) {
		/* Lookup[k] = v: the old value (if any) is dropped; a new key is added */
		Lookup *k = lookup_of(eval_read(I, target->as.index.base));
		Value *key = deref_all(eval_read(I, target->as.index.index));
		bool is_new;
		int64_t pos = lk_put(k, key, &is_new);
		if (!is_new)
			value_drop(I, &k->vals[pos]);
		k->vals[pos] = val;
		return;
	}
	if (target->kind == EXPR_INDEX && target->as.index.on_str) {
		/* s[i] = c: a literal is copied before it is changed */
		Value *sv = deref_all(eval_place(I, target->as.index.base));
		int64_t i = eval(I, target->as.index.index).as.i;
		if (sv->kind != V_STR)
			*sv = (Value){ .kind = V_STR, .as.s = &empty_str };
		str_index(sv->as.s, i, target->loc);
		if (sv->as.s->is_static)
			sv->as.s = str_new(sv->as.s->data, sv->as.s->len);
		if (e->as.assign.compound) {
			Type *ot = e->ty;
			Value old = convert(v_int((int8_t)sv->as.s->data[i]), &t_char, ot);
			Value res = arith(e->as.assign.op, old, val, ot, e->loc);
			val = convert(res, ot, &t_char);
		}
		sv->as.s->data[i] = (char)val.as.i;
		return;
	}
	Value *place;
	if (target->kind == EXPR_INDEX && !target->as.index.lookup_part) {
		Value *base = deref_all(eval_read(I, target->as.index.base));
		if (base->kind == V_ARRAY && base->as.arr->raw) {
			/* an element of an array of numbers: straight into its bytes */
			int64_t idx = eval(I, target->as.index.index).as.i;
			Array *a = base->as.arr;
			set_panic_loc(target->loc);
			rt_bounds_check(idx, a->len, file_of(target->loc), target->loc.line, target->loc.col);
			if (e->as.assign.compound) {
				Type *ot = e->ty, *tt = target->ty;
				Value old = convert(raw_get(a, idx), tt, ot), res;
				if (e->as.assign.op == BIN_MOD && ot->kind == TY_FLOAT)
					res = v_float(fmod(old.as.f, val.as.f));
				else
					res = arith(e->as.assign.op, old, val, ot, e->loc);
				val = convert(res, ot, tt);
			}
			raw_put(a, idx, &val);
			return;
		}
		place = index_place(I, target, base, false);
	} else {
		place = eval_place(I, target);
	}
	if (e->as.assign.compound) {
		Type *ot = e->ty, *tt = target->ty;
		Value old = convert(*place, tt, ot);
		Value res;
		if (e->as.assign.op == BIN_MOD && ot->kind == TY_FLOAT)
			res = v_float(fmod(old.as.f, val.as.f));
		else
			res = arith(e->as.assign.op, old, val, ot, e->loc);
		*place = convert(res, ot, tt);
		return;
	}
	const unsigned char *was = place->kind == V_ADDRESS && place->as.ad ? place->as.ad->raw : NULL;
	if (type_is_plain(target->ty) && (place->kind == V_OBJECT || place->kind == V_ARRAY)) {
		assign_in_place(I, place, val);   /* as in C: the same memory, new contents */
		return;
	}
	if (place->kind != V_UNINIT)
		value_drop(I, place);
	*place = val;
	if (was)
		look_after_overwrite(I, was, e->loc);
}

static void print_value(const Value *v, const Type *t)
{
	switch (v->kind) {
	case V_INT:
		if (t->is_char)
			rt_print_char((uint8_t)v->as.i);
		else if (!t->is_signed)
			rt_print_uint((uint64_t)v->as.i);
		else
			rt_print_int(v->as.i);
		break;
	case V_FLOAT:
		if (t->bits == 32)
			rt_print_float((float)v->as.f);
		else
			rt_print_double(v->as.f);
		break;
	case V_BOOL: rt_print_bool(v->as.b); break;
	case V_STR:  rt_print_str(v->as.s->data, v->as.s->len); break;
	case V_LIST:
		rt_print_list_open();
		for (int64_t i = 0; i < v->as.list->len; i++) {
			rt_print_list_sep(i);
			print_value(&v->as.list->items[i], t->inner);
		}
		rt_print_list_close();
		break;
	case V_LOOKUP: {
		const Lookup *k = v->as.lk;
		rt_print_lookup_open();
		for (int64_t p = 0, i = 0; p < k->n; p++) {
			if (k->dead[p])
				continue;
			rt_print_list_sep(i++);
			print_value(&k->keys[p], t->key);
			rt_print_lookup_colon();
			print_value(&k->vals[p], t->inner);
		}
		rt_print_lookup_close();
		break;
	}
	default:     break;
	}
}

/* ---- extern calls through libffi ---- */

static ffi_type *ffi_of(const Type *t)
{
	switch (t->kind) {
	case TY_BOOL:  return &ffi_type_uint8;
	case TY_INT:
		switch (t->bits) {
		case 8:  return t->is_signed ? &ffi_type_sint8 : &ffi_type_uint8;
		case 16: return t->is_signed ? &ffi_type_sint16 : &ffi_type_uint16;
		case 32: return t->is_signed ? &ffi_type_sint32 : &ffi_type_uint32;
		default: return t->is_signed ? &ffi_type_sint64 : &ffi_type_uint64;
		}
	case TY_FLOAT: return t->bits == 32 ? &ffi_type_float : &ffi_type_double;
	case TY_STR:
	case TY_REF:
	case TY_ADDRESS:
	case TY_CALLBACK: return &ffi_type_pointer;
	default:       return &ffi_type_void;
	}
}

/* ---- callbacks: a Cabby function C can call
 *
 * C is given a function of libffi's making (a closure) with the
 * callback's shape; called, it runs the Cabby function, in whichever
 * interpreter this thread has (a worker's own if C calls from a thread of
 * its own), with the lock held as for a handler. Addresses C passes it
 * stay as C gave them: during qsort they point into the C copy that's
 * being sorted, not at Cabby's values (which are written back after). */
typedef struct {
	ffi_cif cif;
	ffi_type **args;
	FnDecl *fn;
} CallbackShape;

static void worker_interp_begin(Interp *W);
static void worker_interp_end(Interp *W);

static void callback_entry(ffi_cif *cif, void *ret, void **cargs, void *data)
{
	(void)cif;
	CallbackShape *k = data;
	FnDecl *fn = k->fn;
	bool was = rt_gil_enter();
	Interp mine, *I = g_interp;
	if (!I) {
		worker_interp_begin(&mine);
		I = &mine;
	}
	int n = fn->params.len;
	Value *argv = rt_alloc(sizeof *argv * (size_t)(n ? n : 1));
	for (int i = 0; i < n; i++) {
		Type *t = fn->params.items[i]->type->resolved;
		if (t->kind == TY_ADDRESS) {
			void *p;
			memcpy(&p, cargs[i], sizeof p);
			argv[i] = raw_address(p);
		} else {
			argv[i] = decode(t, cargs[i]);
		}
	}
	int mark = I->ntemps;
	Value r = call_function(I, fn, NULL, argv, fn->loc);
	release_temps(I, mark);
	free(argv);
	Type *rt = fn->ret_type;
	/* (libffi wants a whole register's worth for a smaller number) */
	if (rt->kind == TY_INT) {
		if (rt->is_signed)
			*(ffi_sarg *)ret = (ffi_sarg)r.as.i;
		else
			*(ffi_arg *)ret = (ffi_arg)(uint64_t)r.as.i;
	} else if (rt->kind == TY_FLOAT || rt->kind == TY_ADDRESS) {
		encode(rt, &r, ret);
	}
	if (I == &mine)
		worker_interp_end(I);
	rt_gil_leave(was);
}

/* the C function for fn, made the first time it's given to C */
static void *callback_code(FnDecl *fn, SrcLoc at)
{
	if (fn->definition)
		fn = fn->definition;
	if (fn->is_extern)
		return fn->backend ? fn->backend : dlsym(RTLD_DEFAULT, fn->name);   /* C's own */
	if (fn->callback_code)
		return fn->callback_code;
	CallbackShape *k = calloc(1, sizeof *k);
	k->fn = fn;
	int n = fn->params.len;
	k->args = calloc((size_t)(n ? n : 1), sizeof *k->args);
	for (int i = 0; i < n; i++)
		k->args[i] = ffi_of(fn->params.items[i]->type->resolved);
	void *code = NULL;
	ffi_closure *cl = ffi_closure_alloc(sizeof(ffi_closure), &code);
	if (!cl || ffi_prep_cif(&k->cif, FFI_DEFAULT_ABI, (unsigned)n, ffi_of(fn->ret_type), k->args) != FFI_OK ||
	    ffi_prep_closure_loc(cl, &k->cif, callback_entry, k, code) != FFI_OK)
		panic_at(at, "'%s' couldn't be made into a function C can call", fn->name);
	fn->callback_code = code;   /* (kept for the program's life: C may keep it) */
	return code;
}

/* An extern call's own working arrays: kept here until it's done, so a
 * panic it ends in (caught by an `else`) doesn't lose them. */
static _Thread_local void **g_ext;
static _Thread_local int g_next, g_capext;

static void *ext_keep(void *p)
{
	if (g_next == g_capext) {
		g_capext = g_capext ? g_capext * 2 : 32;
		g_ext = realloc(g_ext, sizeof *g_ext * (size_t)g_capext);
	}
	g_ext[g_next++] = p;
	return p;
}

static void ext_release(int mark)
{
	while (g_next > mark)
		rt_free(g_ext[--g_next]);
	if (!g_next) {
		free(g_ext);
		g_ext = NULL;
		g_capext = 0;
	}
}

static Value call_extern(Interp *I, Expr *e, FnDecl *fn)
{
	if (!fn->backend) {
		/* (C's printf is Cabby's, which follows stderr ...) */
		fn->backend = dlsym(RTLD_DEFAULT, strcmp(fn->name, "printf") == 0 ? "cby_printf" : fn->name);
		if (!fn->backend)
#ifdef __wasi__
			panic_at(e->loc, "the C function '%s' isn't here (in a browser)", fn->name);
#else
			panic_at(e->loc, "external function '%s' was not found in any loaded library", fn->name);
#endif
	}
	ExprVec *args = &e->as.call.args;
	int emark = g_next;
	int n = args->len, nfixed = fn->params.len;
	int nargs = n;   /* Cabby arguments (a handler is one, but two C arguments) */
	int nhandlers = 0;
	for (int i = 0; i < nfixed && i < n; i++)
		if (fn->params.items[i]->type->resolved->kind == TY_HANDLER)
			nhandlers++;
	/* a handler is passed as two pointers: the extra ones go after the rest */
	ffi_type **types = ext_keep(rt_alloc(sizeof *types * (size_t)(n + nhandlers + 1)));
	void **values = ext_keep(rt_alloc(sizeof *values * (size_t)(n + nhandlers + 1)));
	unsigned char (*slots)[16] = ext_keep(rt_alloc(16 * (size_t)(n + nhandlers + 1)));     /* passed values */
	HandlerVal **hvs = ext_keep(rt_alloc_zeroed(sizeof *hvs * (size_t)(n + 1)));
	unsigned char (*scratch)[16] = ext_keep(rt_alloc(16 * (size_t)(n + 1)));   /* targets of &scalar args */
	Value **writeback = ext_keep(rt_alloc(sizeof *writeback * (size_t)(n + 1)));
	Type **wb_type = ext_keep(rt_alloc(sizeof *wb_type * (size_t)(n + 1)));
	/* addresses of Cabby's own values: C gets a copy laid out as C lays
	 * it out, which is copied back after the call */
	unsigned char **abuf = ext_keep(rt_alloc_zeroed(sizeof *abuf * (size_t)(n + 1)));
	int xbase = g_nxlat;   /* a call inside an argument keeps its own copies */
	Value **acells = ext_keep(rt_alloc(sizeof *acells * (size_t)(n + 1)));
	int64_t *acount = ext_keep(rt_alloc(sizeof *acount * (size_t)(n + 1)));
	Type **atype = ext_keep(rt_alloc(sizeof *atype * (size_t)(n + 1)));
	AddrVal **aad = ext_keep(rt_alloc(sizeof *aad * (size_t)(n + 1)));

	for (int i = 0; i < n; i++) {
		Expr *a = args->items[i];
		Type *pt = i < nfixed ? fn->params.items[i]->type->resolved : a->ty;
		writeback[i] = NULL;
		if (pt->kind == TY_HANDLER) {
			hvs[i] = eval(I, a).as.hv;
			memcpy(slots[i], &hvs[i]->obj, sizeof(void *));
			types[i] = &ffi_type_pointer;
		} else if (pt->kind == TY_ADDRESS && eval(I, a).as.ad && !eval(I, a).as.ad->raw &&
			   !address_is_none(eval(I, a).as.ad)) {
			AddrVal *ad = eval(I, a).as.ad;
			check_alive(ad, a->loc);
			Type *el = pt->inner ? pt->inner : a->ty->inner;
			if (!el)
				PANIC(a->loc, "an 'address' of nothing in particular can't give C one of Cabby's own values");
			Value *first;
			int64_t count;
			if (ad->arr || ad->list) {
				if (ad->idx < 0 || ad->idx >= seq_len(ad))
					PANIC(a->loc, "this address is outside its %s of %" PRId64, ad->arr ? "array" : "list",
					      seq_len(ad));
				first = &seq_elems(ad)[ad->idx];
				count = seq_len(ad) - ad->idx;   /* C may read on to the end */
			} else {
				if (ad->idx != 0)
					PANIC(a->loc, "this address was stepped %" PRId64 " along from a single value", ad->idx);
				first = ad->cell;
				count = 1;
			}
			uint64_t es = type_size(el);
			abuf[i] = ext_keep(rt_alloc_zeroed((size_t)(es * (uint64_t)count) + 1));
			g_xlat = rt_realloc(g_xlat, sizeof *g_xlat * (size_t)(g_nxlat + 1));
			g_xlat[g_nxlat++] = (Xlat){ abuf[i], es, count, ad };
			for (int64_t k = 0; k < count; k++)
				encode(el, &first[k], abuf[i] + (uint64_t)k * es);
			acells[i] = first;
			acount[i] = count;
			atype[i] = el;
			aad[i] = ad;
			void *p = abuf[i];
			memcpy(slots[i], &p, sizeof p);
			types[i] = &ffi_type_pointer;
		} else if (pt->kind == TY_REF) {
			/* `long &x`: pointer to a C copy, written back after the call */
			Expr *op = a->kind == EXPR_BORROW ? a->as.borrow.operand : a;
			Value *target = deref_all(is_place(op) ? place_to_keep(I, op) : eval_read(I, op));
			encode(pt->inner, target, scratch[i]);
			void *p = scratch[i];
			memcpy(slots[i], &p, sizeof p);
			types[i] = &ffi_type_pointer;
			writeback[i] = target;
			wb_type[i] = pt->inner;
		} else {
			Value *v = deref_all(eval_read(I, a));
			if (v->kind == V_STR) {
				void *p = v->as.s->data;
				memcpy(slots[i], &p, sizeof p);
				types[i] = &ffi_type_pointer;
			} else {
				Type *vt = i < nfixed ? pt : a->ty;
				encode(vt, v, slots[i]);
				types[i] = ffi_of(vt);
			}
		}
		values[i] = slots[i];
	}
	/* handlers: (object, function) — insert each function pointer right
	 * after its object pointer */
	if (nhandlers) {
		int total = n + nhandlers;
		ffi_type **t2 = ext_keep(rt_alloc(sizeof *t2 * (size_t)(total + 1)));
		void **v2 = ext_keep(rt_alloc(sizeof *v2 * (size_t)(total + 1)));
		unsigned char (*s2)[16] = ext_keep(rt_alloc(16 * (size_t)(total + 1)));
		int k = 0;
		for (int i = 0; i < n; i++) {
			memcpy(s2[k], slots[i], 16);
			t2[k] = types[i];
			v2[k] = s2[k];
			k++;
			if (hvs[i]) {
				memcpy(s2[k], &hvs[i]->fn, sizeof(void *));
				t2[k] = &ffi_type_pointer;
				v2[k] = s2[k];
				k++;
			}
		}
		types = t2;
		values = v2;
		slots = s2;
		n = total;
		nfixed += nhandlers;
	}

	Type *rt = fn->ret_type;
	ffi_type *rtype = ffi_of(rt);
	ffi_cif cif;
	ffi_status st = fn->is_variadic
		? ffi_prep_cif_var(&cif, FFI_DEFAULT_ABI, (unsigned)nfixed, (unsigned)n, rtype, types)
		: ffi_prep_cif(&cif, FFI_DEFAULT_ABI, (unsigned)n, rtype, types);
	if (st != FFI_OK)
		panic_at(e->loc, "can't call the C function '%s': libffi couldn't prepare the call", fn->name);

	union { ffi_arg a; ffi_sarg s; double d; float f; int64_t i; unsigned char raw[16]; } ret;
	memset(&ret, 0, sizeof ret);
	bool held = rt_gil_drop();   /* (a worker may run meanwhile) */
	ffi_call(&cif, FFI_FN(fn->backend), &ret, values);
	rt_gil_take(held);

	for (int i = 0; i < nargs; i++)
		if (writeback[i]) {
			Value nv = decode(wb_type[i], scratch[i]);
			*writeback[i] = nv;
		}
	/* an address C gives back inside one it was given (strchr): the same
	 * place in the Cabby value */
	Value from_copy = { .kind = V_UNINIT };
	if (rt->kind == TY_ADDRESS) {
		unsigned char *p;
		memcpy(&p, ret.raw, sizeof p);
		from_copy = xlat_from_c(p);
	}
	for (int i = 0; i < nargs; i++)
		if (abuf[i]) {
			uint64_t es = type_size(atype[i]);
			for (int64_t k = 0; k < acount[i]; k++) {
				Value nv = decode(atype[i], abuf[i] + (uint64_t)k * es);
				value_drop(I, &acells[i][k]);
				acells[i][k] = nv;
			}
		}
	g_nxlat = xbase;   /* the copies are gone */
	if (!g_nxlat) {
		rt_free(g_xlat);
		g_xlat = NULL;
	}
	ext_release(emark);
	if (from_copy.kind == V_ADDRESS)
		return from_copy;

	switch (rt->kind) {
	case TY_BOOL:  return v_bool((uint8_t)ret.a != 0);
	case TY_INT:
		if (rt->bits == 64)
			return v_int(ret.i);
		return v_int(norm(rt->is_signed ? (int64_t)ret.s : (int64_t)ret.a, rt));
	case TY_FLOAT: return v_float(rt->bits == 32 ? (double)ret.f : ret.d);
	case TY_ADDRESS: {
		void *p;
		memcpy(&p, ret.raw, sizeof p);
		return raw_address(p);
	}
	default:       return v_void();
	}
}

/* ---- calls ---- */

static Value eval_call(Interp *I, Expr *e)
{
	ExprVec *args = &e->as.call.args;
	int n = args->len;

	if (e->as.call.builtin > BUILTIN_PRINTLN)
		return eval_str_builtin(I, e);
	if (e->as.call.builtin) {
		/* evaluate every argument before printing anything */
		Value *stack_cells[8];
		Value **cells = n <= 8 ? stack_cells : rt_alloc(sizeof *cells * (size_t)n);
		for (int i = 0; i < n; i++) {
			/* numbers are copied now, so later arguments cannot change them */
			Value *c = eval_read(I, args->items[i]);
			Value *d = deref_all(c);
			cells[i] = (d->kind == V_INT || d->kind == V_FLOAT || d->kind == V_BOOL) ? new_temp(I, *d) : c;
		}
		for (int i = 0; i < n; i++) {
			if (i)
				rt_print_sep();
			print_value(deref_all(cells[i]), args->items[i]->ty);
		}
		if (e->as.call.builtin == BUILTIN_PRINTLN)
			rt_print_newline();
		if (cells != stack_cells)
			rt_free(cells);
		return v_void();
	}

	FnDecl *fn = e->as.call.fn;
	if (fn->definition)
		fn = fn->definition;
	if (fn->is_extern)
		return call_extern(I, e, fn);

	Value stack_args[8];
	Value *argv = n <= 8 ? stack_args : rt_alloc(sizeof(Value) * (size_t)n);
	for (int i = 0; i < n; i++)
		argv[i] = eval(I, args->items[i]);

	/* the receiver is lent after the arguments are evaluated */
	Value self, *selfp = NULL;
	Expr *callee = e->as.call.callee;
	if (callee->kind == EXPR_FIELD && !e->as.call.is_static) {
		self = v_ref(eval_read(I, callee->as.field.object));
		while (self.as.ref->kind == V_REF)
			self = *self.as.ref;
		selfp = &self;
		if (e->as.call.worker_check)
			rt_worker_check(self.as.ref->as.obj, file_of(e->loc), e->loc.line, e->loc.col);
		if (e->as.call.slot >= 0)
			fn = self.as.ref->as.obj->cls->vtable[e->as.call.slot];   /* dynamic dispatch */
	}

	Value r = call_function(I, fn, selfp, argv, e->loc);
	if (argv != stack_args)
		rt_free(argv);
	return r;
}

static Value *cells_push(Interp *I, int64_t n);
static void cells_pop(Interp *I, int64_t n);

/* A value being made, held in a cell of its own meanwhile: a panic while
 * its parts are worked out (caught by an `else`) frees it with the rest. */
#define MAX_HOLDS 256
static _Thread_local Value *g_holds[MAX_HOLDS];   /* (nested deeper than this: not kept track of) */
static _Thread_local int64_t g_nholds;

static Value *hold_value(Interp *I, Value v)
{
	Value *h = cells_push(I, 1);
	*h = v;
	if (g_nholds < MAX_HOLDS)
		g_holds[g_nholds] = h;
	g_nholds++;
	return h;
}

static Value let_go_of(Interp *I, Value *h)
{
	Value v = *h;
	h->kind = V_UNINIT;
	g_nholds--;
	cells_pop(I, 1);
	return v;
}

static Value eval_init(Interp *I, Expr *e)
{
	Type *t = e->ty;
	Expr **slots = e->as.init.slots;
	if (t->kind == TY_LOOKUP) {
		Value *h = hold_value(I, zero_value(t));
		#define v (*h)
		for (int i = 0; i < e->as.init.nslots; i += 2) {
			Value val = eval(I, slots[i + 1]);
			Value *key = deref_all(eval_read(I, slots[i]));
			bool is_new;
			int64_t pos = lk_put(v.as.lk, key, &is_new);
			if (!is_new)
				value_drop(I, &v.as.lk->vals[pos]);
			v.as.lk->vals[pos] = val;
		}
		#undef v
		return let_go_of(I, h);
	}
	if (t->kind == TY_LIST) {
		Value *h = hold_value(I, zero_value(t));
		List *l = h->as.list;
		list_reserve(l, e->as.init.nslots);
		for (int i = 0; i < e->as.init.nslots; i++) {
			Value x = eval(I, slots[i]);
			l->items[l->len++] = x;
		}
		return let_go_of(I, h);
	}
	if (t->kind == TY_ARRAY) {
		Array *a = array_new(t->inner, (int64_t)t->len);
		if (a->raw) {
			for (uint64_t i = 0; i < t->len; i++)
				if (slots[i]) {
					Value x = eval(I, slots[i]);
					raw_put(a, (int64_t)i, &x);
				}
			return (Value){ .kind = V_ARRAY, .as.arr = a };
		}
		for (uint64_t i = 0; i < t->len; i++)
			a->elems[i].kind = V_UNINIT;
		for (uint64_t i = 0; i < t->len; i++)
			a->elems[i] = slots[i] ? eval(I, slots[i]) : zero_value(t->inner);
		return (Value){ .kind = V_ARRAY, .as.arr = a };
	}
	ObjectInfo *o = t->obj;
	if (o->kind == OBJ_UNION) {
		Value u = zero_value(t);
		for (int i = 0; i < o->field_count; i++)
			if (slots[i]) {
				Value v = eval(I, slots[i]);
				encode(o->fields[i].type, &v, u.as.un->bytes + o->fields[i].offset);
				value_drop(I, &v);
			}
		return u;
	}
	Object *ob = rt_alloc(sizeof *ob + sizeof(Value) * (size_t)o->field_count);
	ob->life = 0;
	ob->cls = o;
	for (int i = 0; i < o->field_count; i++)
		ob->fields[i].kind = V_UNINIT;
	Value *h = hold_value(I, (Value){ .kind = V_OBJECT, .as.obj = ob });
	for (int i = 0; i < o->field_count; i++) {
		Value x = slots[i] ? eval(I, slots[i]) : zero_value(o->fields[i].type);
		ob->fields[i] = x;
	}
	run_makers(ob);   /* once its fields are set */
	return let_go_of(I, h);
}

/* L's object, for L.Name or L.speak: where it is, as a value (a panic if
 * it's gone) */
static Value *link_get(Interp *I, Expr *e)
{
	int64_t l = eval(I, e->as.link.operand).as.i;
	set_panic_loc(e->loc);
	Object **h = cby_link_get(l, e->as.link.name, e->ty->obj->name, file_of(e->loc), e->loc.line, e->loc.col);
	return &(*h)->as_value;
}

/* a link to the object at a place */
static Value link_to(Interp *I, Expr *e)
{
	Object *o = deref_all(eval_place(I, e->as.link.operand))->as.obj;
	o->as_value = (Value){ .kind = V_OBJECT, .as.obj = o };
	return v_int(cby_link_to(o, &o->life));
}

static Value eval(Interp *I, Expr *e)
{
	switch (e->kind) {
	case EXPR_LINK_TO:
		return link_to(I, e);
	case EXPR_INT:
		return v_int(norm((int64_t)e->as.int_lit.value, e->ty));
	case EXPR_FLOAT:
		return v_float(e->ty->bits == 32 ? (double)(float)e->as.float_lit.value : e->as.float_lit.value);
	case EXPR_BOOL:   return v_bool(e->as.bool_val);
	case EXPR_CHAR:   return v_int((int8_t)e->as.char_val);
	case EXPR_STRING: return string_literal(I, e);

	case EXPR_IDENT:
	case EXPR_SELF: {
		Symbol *s = e->as.ident.sym;
		Value *cell = symbol_cell(I, s);
		if (e->use == USE_MOVE && s->type->kind != TY_REF) {
			Value v = *cell;
			cell->kind = V_UNINIT;   /* clears the drop flag */
			return v;
		}
		return value_copy(cell, e->loc);
	}

	case EXPR_UNARY: {
		Expr *op = e->as.unary.operand;
		switch (e->as.unary.op) {
		case UN_NEG: {
			if (op->kind == EXPR_INT)
				return v_int(norm((int64_t)(0 - op->as.int_lit.value), e->ty));
			Value v = eval(I, op);
			if (v.kind == V_FLOAT)
				return v_float(-v.as.f);
			return v_int(norm((int64_t)(0 - (uint64_t)v.as.i), e->ty));
		}
		case UN_NOT:    return v_bool(!eval(I, op).as.b);
		case UN_BITNOT: return v_int(norm(~eval(I, op).as.i, e->ty));
		}
		break;
	}

	case EXPR_CALLBACK:
		return v_int((int64_t)(intptr_t)callback_code(e->as.callback.fn, e->loc));

	case EXPR_HANDLER: {
		Value *w = deref_all(symbol_cell(I, e->as.handler.self));
		Object *o = w->as.obj;
		FnDecl *fn = e->as.handler.slot >= 0 ? o->cls->vtable[e->as.handler.slot] : e->as.handler.method;
		if (fn->definition)
			fn = fn->definition;
		HandlerVal *hv = arena_alloc(lasting(I), sizeof *hv);
		hv->obj = o;
		hv->fn = fn;
		return (Value){ .kind = V_HANDLER, .as.hv = hv };
	}

	case EXPR_BORROW: {
		Expr *op = e->as.borrow.operand;
		Value *target = is_place(op) ? place_to_keep(I, op) : new_temp(I, eval(I, op));
		return v_ref(target);
	}

	case EXPR_BINARY:
		return eval_binary(I, e);

	case EXPR_ASSIGN:
		exec_assign(I, e);
		return v_void();

	case EXPR_CALL:
		return eval_call(I, e);

	case EXPR_INDEX:
		if (!e->as.index.lookup_part) {   /* an element of an array of numbers: read straight from its bytes */
			Value *base = deref_all(eval_read(I, e->as.index.base));
			if (base->kind == V_ARRAY && base->as.arr->raw) {
				int64_t idx = eval(I, e->as.index.index).as.i;
				Array *a = base->as.arr;
				set_panic_loc(e->loc);
				rt_bounds_check(idx, a->len, file_of(e->loc), e->loc.line, e->loc.col);
				return raw_get(a, idx);
			}
			return value_copy(index_place(I, e, base, false), e->loc);
		}
		return value_copy(eval_place(I, e), e->loc);
	case EXPR_FIELD:
	case EXPR_AT:
	case EXPR_LINK_GET:
		return value_copy(eval_place(I, e), e->loc);

	case EXPR_CAST: {
		Value v = eval(I, e->as.cast.value);
		return convert(v, e->as.cast.value->ty, e->ty);
	}

	case EXPR_INIT:
		return eval_init(I, e);

	case EXPR_SIZEOF:
		break;
	}
	internal_error(e->loc, "unexpected expression");
}

/* ============================================================== statements */

static Exec exec_stmt(Interp *I, Stmt *s);

static void drop_locals(Interp *I, Block *b)
{
	for (int i = b->locals.len - 1; i >= 0; i--) {
		Value *c = &I->frame->cells[b->locals.items[i]->index];
		if (c->kind != V_UNINIT)
			value_drop(I, c);
		if (g_nlive_watches) {
			char why[96];
			FnDecl *fn = I->frame->fn;
			if (fn && b == fn->body)
				snprintf(why, sizeof why, "'%s' returned", fn->name);
			else
				snprintf(why, sizeof why, "the block it was declared in ended");
			places_gone(c, c + 1, why);
		}
	}
}

static Exec exec_block_from(Interp *I, Block *b, int start)
{
	Exec ex = EX_NORMAL;
	for (int i = start; i < b->stmts.len; i++) {
		int mark = I->ntemps;
		ex = exec_stmt(I, b->stmts.items[i]);
		release_temps(I, mark);
		if (ex == EX_GOTO && I->goto_target->as.label.block == b) {
			i = I->goto_target->as.label.index;   /* the label itself does nothing */
			I->goto_target = NULL;
			ex = EX_NORMAL;
			continue;
		}
		if (ex != EX_NORMAL)
			break;
	}
	drop_locals(I, b);
	return ex;
}

static Exec exec_block(Interp *I, Block *b) { return exec_block_from(I, b, 0); }

static bool eval_cond(Interp *I, Expr *cond)
{
	int mark = I->ntemps;
	bool b = eval(I, cond).as.b;
	release_temps(I, mark);
	return b;
}

/* ---- interactive session: Ctrl-C and panics return to the prompt ---- */

volatile sig_atomic_t interp_interrupted = 0;
static jmp_buf *g_session_jmp = NULL;

/* Workers (use threads) take turns with the rest of the program: every so
 * many calls and loops, another thread waiting to run Cabby code has a
 * turn (rt_gil_yield, in runtime/threads.c) */
static _Thread_local int g_turns = 1000;

static void check_interrupt(void)
{
	if (--g_turns <= 0) {
		g_turns = 1000;
		if (rt_gil_is_on())
			rt_gil_yield();
	}
	if (interp_interrupted && g_session_jmp && g_interp == g_main_interp) {   /* (a worker carries on) */
		interp_interrupted = 0;
		longjmp(*g_session_jmp, 2);
	}
}

/* Runs a loop body. Returns true if the loop must stop; *out is the status
 * to propagate (EX_NORMAL after a break). */
static bool loop_body(Interp *I, Block *body, Exec *out)
{
	check_interrupt();
	Exec ex = exec_block(I, body);
	switch (ex) {
	case EX_BREAK:
		*out = EX_NORMAL;
		return true;
	case EX_RETURN:
	case EX_GOTO:
		*out = ex;
		return true;
	default:
		return false;
	}
}

static Exec exec_switch(Interp *I, Stmt *s)
{
	int mark = I->ntemps;
	int64_t v = eval(I, s->as.switch_.value).as.i;
	release_temps(I, mark);
	CaseVec *cases = &s->as.switch_.cases;
	int start = -1;
	for (int i = 0; i < cases->len && start < 0; i++)
		for (int j = 0; j < cases->items[i].values.len; j++)
			if (cases->items[i].consts[j] == v)
				start = i;
	if (start < 0)
		start = s->as.switch_.default_index;
	if (start < 0)
		return EX_NORMAL;
	for (int i = start; i < cases->len; i++) {
		Exec ex = exec_block(I, cases->items[i].body);
		if (ex == EX_BREAK)
			return EX_NORMAL;
		if (ex != EX_NORMAL)
			return ex;
	}
	return EX_NORMAL;
}

static Exec exec_for(Interp *I, Stmt *s)
{
	Exec out = EX_NORMAL;
	if (s->as.for_.init) {
		int mark = I->ntemps;
		exec_stmt(I, s->as.for_.init);
		release_temps(I, mark);
	}
	for (;;) {
		if (s->as.for_.cond && !eval_cond(I, s->as.for_.cond))
			break;
		if (loop_body(I, s->as.for_.body, &out))
			break;
		if (s->as.for_.update) {
			int mark = I->ntemps;
			exec_stmt(I, s->as.for_.update);
			release_temps(I, mark);
		}
	}
	drop_locals(I, s->as.for_.scope);
	return out;
}

static Exec exec_stmt_itself(Interp *I, Stmt *s);

/* What a panic jumped out of, let go: values half made (freed), then the
 * temporaries made since and the calls it was in (their cells, on the
 * interpreter's own stack of cells), dropped the last made first, their
 * destructors run, as a compiled program's are. */
static void recover_to(Interp *I, CellChunk *chunk, int64_t used, int ntemps, int64_t holds)
{
	while (g_nholds > holds) {
		if (--g_nholds < MAX_HOLDS) {
			Value *h = g_holds[g_nholds];
			value_free(h);
			h->kind = V_UNINIT;
		}
	}
	while (I->ntemps > ntemps) {
		Temp *t = I->temps[--I->ntemps];
		Value v = t->cell;
		t->cell.kind = V_UNINIT;
		value_drop(I, &v);
		rt_free(t);
	}
	for (;;) {
		CellChunk *c = I->chunk;
		if (!c || (c == chunk && c->used <= used) || (!chunk && !c->prev && c->used == 0))
			break;
		if (c->used == 0) {   /* (an empty chunk on top: the one under it) */
			I->chunk = c->prev;
			rt_free(c);
			continue;
		}
		Value v = c->cells[c->used - 1];
		c->cells[c->used - 1].kind = V_UNINIT;
		cells_pop(I, 1);
		value_drop(I, &v);
	}
}

/* `... else`: the statement, and if it panics, its else part */
static Exec exec_recovering(Interp *I, Stmt *s)
{
	Recover *r = s->recover;
	CellChunk *chunk = I->chunk;
	/* (volatile: newer gcc can't tell it isn't changed after _setjmp) */
	volatile int64_t used = chunk ? chunk->used : 0;
	int ntemps = I->ntemps, depth = I->depth, drop_depth = g_drop_depth;
	int panic_file = g_panic_file, lib_file = g_lib_call_fileid;
	int ext_mark = g_next, nxlat = g_nxlat;
	int64_t holds = g_nholds;
	Frame *frame = I->frame;
	RtTry t;
	rt_try_push(&t);
	if (_setjmp(t.jb) == 0) {
		Exec ex = exec_stmt_itself(I, s);
		rt_try_pop(&t);
		return ex;
	}
	I->depth = depth;
	I->frame = frame;
	I->goto_target = NULL;
	g_drop_depth = drop_depth;
	g_panic_file = panic_file;
	g_lib_call_fileid = lib_file;
	ext_release(ext_mark);
	g_nxlat = nxlat;
	recover_to(I, chunk, used, ntemps, holds);
	if (s->kind == STMT_DECL && r->value) {
		Symbol *sym = s->as.decl.items[0]->sym;
		I->frame->cells[sym->index] = eval(I, r->value);
		return EX_NORMAL;
	}
	return r->block ? exec_block(I, r->block) : EX_NORMAL;
}

static Exec exec_stmt(Interp *I, Stmt *s)
{
	if (s->recover)
		return exec_recovering(I, s);
	return exec_stmt_itself(I, s);
}

static Exec exec_stmt_itself(Interp *I, Stmt *s)
{
	switch (s->kind) {
	case STMT_DECL:
		for (int i = 0; i < s->as.decl.len; i++) {
			VarDecl *v = s->as.decl.items[i];
			Symbol *sym = v->sym;
			if (sym->kind == SYM_GLOBAL)
				continue;   /* static locals live for the whole program */
			Value *cell = &I->frame->cells[sym->index];
			*cell = v->init ? eval(I, v->init) : zero_value(sym->type);
		}
		return EX_NORMAL;

	case STMT_EXPR: {
		Expr *e = s->as.expr;
		if (e->kind == EXPR_CALL) {
			Value v = eval(I, e);
			value_drop(I, &v);   /* unused result */
		} else if (e->kind == EXPR_ASSIGN) {
			exec_assign(I, e);
		} else {
			eval_read(I, e);
		}
		return EX_NORMAL;
	}

	case STMT_IF:
		if (eval_cond(I, s->as.if_.cond))
			return exec_block(I, s->as.if_.then_block);
		if (s->as.if_.else_branch)
			return exec_stmt(I, s->as.if_.else_branch);
		return EX_NORMAL;

	case STMT_BLOCK:
		return exec_block(I, s->as.block);

	case STMT_WHILE: {
		Exec out = EX_NORMAL;
		while (eval_cond(I, s->as.while_.cond))
			if (loop_body(I, s->as.while_.body, &out))
				break;
		return out;
	}

	case STMT_DO: {
		Exec out = EX_NORMAL;
		do {
			if (loop_body(I, s->as.while_.body, &out))
				break;
		} while (eval_cond(I, s->as.while_.cond));
		return out;
	}

	case STMT_FOR:
		return exec_for(I, s);

	case STMT_SWITCH:
		return exec_switch(I, s);

	case STMT_RETURN:
		I->frame->ret = s->as.ret.value ? eval(I, s->as.ret.value) : v_void();
		return EX_RETURN;

	case STMT_BREAK:    return EX_BREAK;
	case STMT_CONTINUE: return EX_CONTINUE;
	case STMT_LABEL:    return EX_NORMAL;
	case STMT_GOTO:
		I->goto_target = s->as.goto_.target;
		return EX_GOTO;
	}
	return EX_NORMAL;
}

/* ============================================================== functions */

static Value *cells_push(Interp *I, int64_t n)
{
	CellChunk *c = I->chunk;
	if (!c || c->used + n > c->cap) {
		int64_t cap = n > 65536 ? n : 65536;
		CellChunk *nc;
		if (I->spare && I->spare->cap >= n) {
			nc = I->spare;
			I->spare = NULL;
		} else {
			nc = rt_alloc(sizeof *nc + sizeof(Value) * (size_t)cap);
			nc->cap = cap;
		}
		nc->used = 0;
		nc->prev = c;
		I->chunk = c = nc;
	}
	Value *v = c->cells + c->used;
	c->used += n;
	memset(v, 0, sizeof(Value) * (size_t)n);
	return v;
}

static void cells_pop(Interp *I, int64_t n)
{
	CellChunk *c = I->chunk;
	c->used -= n;
	if (c->used == 0 && c->prev) {
		I->chunk = c->prev;
		if (I->spare)
			rt_free(I->spare);
		I->spare = c;
	}
}

/* At the end: give back the call stack and the spare temporaries. */
static void interp_free_pools(Interp *I)
{
	while (I->chunk) {
		CellChunk *c = I->chunk;
		I->chunk = c->prev;
		rt_free(c);
	}
	rt_free(I->spare);
	I->spare = NULL;
	while (I->free_temps) {
		Temp *t = I->free_temps;
		I->free_temps = t->next_free;
		rt_free(t);
	}
}

/* After a panic in the interactive session: every call it was in is gone. */
static void cells_reset(Interp *I)
{
	while (I->chunk && I->chunk->prev) {
		CellChunk *c = I->chunk;
		I->chunk = c->prev;
		rt_free(c);
	}
	if (I->chunk)
		I->chunk->used = 0;
}

#include "interpreter_bytecode.c"

static Value call_function(Interp *I, FnDecl *fn, Value *self, Value *args, SrcLoc loc)
{
	check_interrupt();
	if (++I->depth > (I->max_depth ? I->max_depth : MAX_DEPTH)) {
		set_panic_loc(loc);
		rt_panic(file_of(loc), loc.line, loc.col, "stack overflow: more than %d nested calls (calling '%s')",
			 I->max_depth ? I->max_depth : MAX_DEPTH, fn->name);
	}
	Frame f;
	int ncells = fn->frame.len;
	Bytecode *bc = bc_of(fn);
	int room = ncells + (bc ? bc->ntemps : 0);
	f.cells = cells_push(I, room ? room : 1);
	f.ncells = ncells;
	f.ret = v_void();
	f.caller = I->frame;
	f.fn = fn;
	if (fn->self_sym)
		f.cells[fn->self_sym->index] = *self;
	for (int i = 0; i < fn->params.len; i++)
		f.cells[fn->params.items[i]->sym->index] = args[i];

	Frame *saved = I->frame;
	I->frame = &f;
	/* the program calling into a library: where, for a panic inside it */
	const char *was_file = rt_lib_call_file;
	int was_line = rt_lib_call_line, was_col = rt_lib_call_col;
	bool lib_call = is_header_file(fn->loc.file) && !is_header_file(loc.file);
	int was_fileid = g_lib_call_fileid;
	if (lib_call) {
		rt_set_lib_call(file_of(loc), loc.line, loc.col);
		g_panic_file = loc.file;
		g_lib_call_fileid = loc.file;
	}
	if (bc)
		bc_run(I, bc);
	else
		exec_block(I, fn->body);
	if (lib_call) {
		rt_set_lib_call(was_file, was_line, was_col);
		g_lib_call_fileid = was_fileid;
	}
	/* locals were dropped by their blocks; now the parameters, in reverse */
	for (int i = ncells - 1; i >= 0; i--)
		if (f.cells[i].kind != V_UNINIT)
			value_drop(I, &f.cells[i]);
	if (g_nlive_watches) {
		char why[96];
		snprintf(why, sizeof why, "'%s' returned", fn->name);
		places_gone(f.cells, f.cells + ncells, why);
	}
	I->frame = saved;
	cells_pop(I, room ? room : 1);
	I->depth--;
	return f.ret;
}

static void show_panic_line(int line, int col)
{
	diag_show_line_in(rt_panic_at_lib_call && g_lib_call_fileid >= 0 ? g_lib_call_fileid : g_panic_file, line, col);
	rt_panic_at_lib_call = false;   /* (the interactive session goes on) */
}

/* ---- native libraries and handlers ----
 * A library header (`use gui`) may come with native code, libcabby<name>.so
 * in its directory: it is loaded here. If it calls handlers back, it
 * exports `cby_invoke_hook`, which is pointed at interp_invoke. */


/* A worker's thread has an interpreter of its own: its own calls and
 * temporaries, and the program's globals */
static void worker_interp_begin(Interp *W)
{
	memset(W, 0, sizeof *W);
	W->S = g_main_interp->S;
	W->globals = g_main_interp->globals;
	W->argc = g_main_interp->argc;
	W->argv = g_main_interp->argv;
	W->max_depth = 25000;   /* (a worker's thread has 256 MB) */
	arena_init(&W->lits);
	g_interp = W;
}

static void worker_interp_end(Interp *W)
{
	release_temps(W, 0);
	free(W->temps);
	interp_free_pools(W);
	arena_free(&W->lits);
	g_interp = NULL;
}

static void interp_invoke(void *obj, void *fn)
{
	bool was = rt_gil_enter();
	Interp mine, *I = g_interp;
	if (!I) {
		worker_interp_begin(&mine);
		I = &mine;
	}
	Value cell = { .kind = V_OBJECT, .as.obj = obj };
	Value self = v_ref(&cell);
	int mark = I->ntemps;
	call_function(I, fn, &self, NULL, ((FnDecl *)fn)->loc);
	release_temps(I, mark);
	fflush(stdout);
	rt_flush_files();   /* a window waits between handlers: files are up to date */
	if (I == &mine)
		worker_interp_end(I);
	rt_gil_leave(was);
}

static void load_natives(Program *prog)
{
#ifdef __wasi__
	/* (WebAssembly: no libraries to load; what's in them is missing) */
	(void)prog;
	return;
#endif
	for (int i = 0; i < prog->natives.len; i++) {
		NativeLib *nl = &prog->natives.items[i];
		char path[4096];
		snprintf(path, sizeof path, "%s/libcabby%s.so", nl->dir, nl->name);
		FILE *f = fopen(path, "r");
		if (!f)
			continue;   /* a header of plain declarations */
		fclose(f);
		void *h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
		if (!h) {
			fprintf(stderr, "cabby: can't load %s: %s\n", path, dlerror());
			exit(1);
		}
		void (**hook)(void *, void *) = (void (**)(void *, void *))dlsym(h, "cby_invoke_hook");
		if (hook)
			*hook = interp_invoke;
	}
}

typedef struct {
	Interp *I;
	int status;
} RunArgs;

static void *run_main(void *p)
{
	RunArgs *ra = p;
	Interp *I = ra->I;
	Sema *S = I->S;
	g_interp = I;   /* (this thread's) */
	bool was = rt_gil_enter();

	/* globals and static locals, in declaration order */
	Frame none = { .cells = NULL };
	I->frame = &none;
	for (int i = 0; i < S->globals.len; i++) {
		Symbol *g = S->globals.items[i];
		VarDecl *v = g->decl;
		*I->globals[i] = v->init ? eval(I, v->init) : zero_value(g->type);
	}
	I->frame = NULL;

	FnDecl *m = S->main_fn;
	Value args[2];
	if (m->params.len == 2) {
		Array *a = array_new(NULL, I->argc);
		for (int i = 0; i < I->argc; i++)
			a->elems[i] = (Value){ .kind = V_STR, .as.s = str_new(I->argv[i], (int64_t)strlen(I->argv[i])) };
		I->argv_cell = (Value){ .kind = V_ARRAY, .as.arr = a };
		args[0] = v_int(I->argc);
		args[1] = v_ref(&I->argv_cell);
	}
	Value r = call_function(I, m, NULL, args, m->loc);
	ra->status = r.kind == V_INT ? (int)(r.as.i & 0xFF) : 0;
	rt_gil_leave(was);
	rt_thread_ends();   /* (the program's own thread: what the libraries kept for it) */
	return NULL;
}

/* Programs run on a thread of their own, with a big stack (a deep
 * recursion is a panic, not a crash) -- except on macOS, where a program
 * with windows must run on the first thread, the only one that may draw. */
static bool on_main_thread(Program *prog)
{
#if defined(__APPLE__)
	for (int i = 0; i < prog->natives.len; i++) {
		const char *n = prog->natives.items[i].name;
		if (!strcmp(n, "gui") || !strcmp(n, "game") || !strcmp(n, "gtk") || !strcmp(n, "tgame") || !strcmp(n, "world") ||
		    !strcmp(n, "bluetooth"))
			return true;
	}
#else
	(void)prog;
#endif
	return false;
}

static size_t main_stack_size(void)
{
#if defined(__APPLE__)
	return pthread_get_stacksize_np(pthread_self());
#else
	return (size_t)8 << 20;
#endif
}

int interpret(Sema *sema, int argc, char **argv)
{
	Interp I;
	memset(&I, 0, sizeof I);
	I.S = sema;
	I.argc = argc;
	I.argv = argv;
	int ng = sema->globals.len;
	Value *gcells = rt_alloc_zeroed(sizeof(Value) * (size_t)(ng ? ng : 1));
	I.globals = rt_alloc(sizeof(Value *) * (size_t)(ng ? ng : 1));
	for (int i = 0; i < ng; i++)
		I.globals[i] = &gcells[i];
	arena_init(&I.lits);
	rt_panic_hook = show_panic_line;

	g_interp = &I;
	g_main_interp = &I;
	const char *bco = getenv("CABBY_BYTECODE");
	g_bc_off = bco && !strcmp(bco, "0");
	rt_invoke_hook = interp_invoke;   /* timers call methods through the interpreter */
	load_natives(sema->prog);
	RunArgs ra = { &I, 0 };
#ifdef __wasi__
	/* (WebAssembly: one thread. Its calls' own stack is the browser's,
	 * which holds about 1500 of the interpreter's nested calls at the
	 * most: 1000, so a panic says so before the browser stops it) */
	I.max_depth = 1000;
	run_main(&ra);
#else
	if (on_main_thread(sema->prog)) {
		/* macOS draws windows only from the program's first thread: a
		 * window program runs there, on its stack (as many nested calls as
		 * it has room for) */
		size_t room = main_stack_size();
		I.max_depth = room / 10240 < 1000 ? 1000 : (int)(room / 10240);
		if (I.max_depth > MAX_DEPTH)
			I.max_depth = MAX_DEPTH;
		run_main(&ra);
	} else {
		pthread_attr_t attr;
		pthread_t th;
		bool threaded = pthread_attr_init(&attr) == 0 &&
				pthread_attr_setstacksize(&attr, INTERP_STACK_SIZE) == 0 &&
				pthread_create(&th, &attr, run_main, &ra) == 0;
		if (threaded)
			pthread_join(th, NULL);
		else
			run_main(&ra);
		pthread_attr_destroy(&attr);
	}
#endif

	fflush(stdout);
	release_temps(&I, 0);
	free(I.temps);
	report_leaks();
	for (int i = 0; i < ng; i++)
		value_drop(&I, I.globals[i]);
	rt_free(I.globals);
	rt_free(gcells);
	interp_free_pools(&I);
	bc_free_all();
	str_pool_free();
	if (I.argv_cell.kind != V_UNINIT)
		value_drop(&I, &I.argv_cell);
	arena_free(&I.lits);
	rt_panic_hook = NULL;
	return ra.status;
}

/* ============================================================== interactive session */

#define SESSION_CELLS (1 << 20)

typedef struct GlobalSlot {
	char *key;
	Value *cell;
} GlobalSlot;

struct InterpSession {
	Interp I;
	Sema *S;
	Frame main;              /* main's frame: never moves, so borrows of it stay valid */
	int committed;           /* frame length after the last input that ran to completion */
	unsigned char *before;   /* cell kinds before the current run */
	GlobalSlot *gtab;
	int ngtab, capgtab;
	jmp_buf jb;
};

static void map_panic(const char **file, int *line, int *col)
{
	DiagPos p = diag_map(g_panic_file, *line, *col);
	*file = p.name;
	*line = p.line;
	*col = p.col;
}

InterpSession *isession_new(void)
{
	InterpSession *IS = calloc(1, sizeof *IS);
	IS->main.cells = calloc(SESSION_CELLS, sizeof(Value));
	if (!IS || !IS->main.cells)
		rt_panic(NULL, 0, 0, "out of memory");
	arena_init(&IS->I.lits);
	rt_panic_hook = show_panic_line;
	rt_panic_map = map_panic;
#if defined(__APPLE__)
	/* (on macOS the session runs on the first thread, so windows can open:
	 * as many nested calls as its stack has room for) */
	size_t room = main_stack_size();
	IS->I.max_depth = room / 10240 < 1000 ? 1000 : room / 10240 > MAX_DEPTH ? MAX_DEPTH : (int)(room / 10240);
#endif
	return IS;
}

/* Points an object at the newest description of its class (the old one
 * belongs to a syntax tree that is about to be freed). */
static void patch_classes(Sema *S, Value *v)
{
	ObjectInfo **cls = NULL;
	if (v->kind == V_OBJECT)
		cls = &v->as.obj->cls;
	else if (v->kind == V_UNION)
		cls = &v->as.un->cls;
	else if (v->kind == V_ARRAY) {
		for (int64_t i = 0; v->as.arr->raw ? false : i < v->as.arr->len; i++)
			patch_classes(S, &v->as.arr->elems[i]);
		return;
	} else if (v->kind == V_LIST) {
		for (int64_t i = 0; i < v->as.list->len; i++)
			patch_classes(S, &v->as.list->items[i]);
		return;
	} else if (v->kind == V_LOOKUP) {
		Lookup *k = v->as.lk;
		for (int64_t p = 0; p < k->n; p++)
			if (!k->dead[p]) {
				patch_classes(S, &k->keys[p]);
				patch_classes(S, &k->vals[p]);
			}
		return;
	} else if (v->kind == V_HANDLER) {
		/* the method, in the newest description of its object's classes
		 * (the object itself is patched where it is held) */
		HandlerVal *hv = v->as.hv;
		if (!hv || !hv->fn || !hv->obj)
			return;
		const char *name = ((FnDecl *)hv->fn)->name;
		ObjectInfo *oc = NULL;
		const char *cname = ((Object *)hv->obj)->cls->name;
		for (int i = 0; i < S->nobjects; i++)
			if (strcmp(S->objects[i]->name, cname) == 0)
				oc = S->objects[i];
		for (int m = 0; oc && m < oc->mro_len; m++) {
			FnDeclVec *ms = &oc->mro[m]->decl->methods;
			for (int j = 0; j < ms->len; j++)
				if (strcmp(ms->items[j]->name, name) == 0) {
					hv->fn = ms->items[j];
					return;
				}
		}
		return;
	} else {
		return;
	}
	for (int i = 0; i < S->nobjects; i++)
		if (strcmp(S->objects[i]->name, (*cls)->name) == 0) {
			*cls = S->objects[i];
			break;
		}
	if (v->kind == V_OBJECT)
		for (int i = 0; i < (*cls)->field_count; i++)
			patch_classes(S, &v->as.obj->fields[i]);
}

bool isession_adopt(InterpSession *IS, Sema *S, const char *(*key)(Symbol *g, void *ctx), void *ctx)
{
	if (S->main_fn->frame.len > SESSION_CELLS)
		return false;
	Interp *I = &IS->I;
	IS->S = S;
	I->S = S;
	IS->main.ncells = S->main_fn->frame.len;
	g_interp = I;
	g_main_interp = I;
	g_bc_off = true;   /* (the session's main is checked again with each input) */
	rt_invoke_hook = interp_invoke;
	load_natives(S->prog);

	/* globals and static locals keep their cells from earlier inputs */
	int ng = S->globals.len;
	Value **g = rt_alloc(sizeof *g * (size_t)(ng ? ng : 1));
	bool *fresh = rt_alloc_zeroed((size_t)(ng ? ng : 1));
	for (int i = 0; i < ng; i++) {
		const char *k = key(S->globals.items[i], ctx);
		g[i] = NULL;
		for (int j = 0; j < IS->ngtab; j++)
			if (strcmp(IS->gtab[j].key, k) == 0)
				g[i] = IS->gtab[j].cell;
		if (!g[i]) {
			if (IS->ngtab == IS->capgtab) {
				IS->capgtab = IS->capgtab ? IS->capgtab * 2 : 16;
				IS->gtab = realloc(IS->gtab, sizeof *IS->gtab * (size_t)IS->capgtab);
			}
			g[i] = rt_alloc_zeroed(sizeof(Value));
			IS->gtab[IS->ngtab++] = (GlobalSlot){ strdup(k), g[i] };
			fresh[i] = true;
		}
	}
	rt_free(I->globals);
	I->globals = g;

	Frame none = { .cells = NULL };
	I->frame = &none;
	for (int i = 0; i < ng; i++)
		if (fresh[i]) {
			VarDecl *v = S->globals.items[i]->decl;
			*g[i] = v->init ? eval(I, v->init) : zero_value(S->globals.items[i]->type);
		}
	rt_free(fresh);
	release_temps(I, 0);
	I->frame = &IS->main;

	for (int i = 0; i < S->main_fn->frame.len; i++)
		patch_classes(S, &IS->main.cells[i]);
	for (int i = 0; i < IS->ngtab; i++)
		patch_classes(S, IS->gtab[i].cell);
	return true;
}

IRun isession_run(InterpSession *IS, int first, int *status)
{
	Interp *I = &IS->I;
	Sema *S = IS->S;
	Block *body = S->main_fn->body;
	int n = S->main_fn->frame.len;

	free(IS->before);
	IS->before = malloc((size_t)(IS->committed ? IS->committed : 1));
	for (int i = 0; i < IS->committed; i++)
		IS->before[i] = (unsigned char)IS->main.cells[i].kind;

	I->frame = &IS->main;
	g_session_jmp = &IS->jb;
	rt_panic_jmp = &IS->jb;
	int why = setjmp(IS->jb);
	if (why) {
		rt_out_reset();     /* a panic inside `stderr ...`: print to stdout again */
		/* Abandon the half-run input. Its temporaries and any frames it was
		 * in are leaked rather than dropped: their state is unknown. */
		g_session_jmp = NULL;
		rt_panic_jmp = NULL;
		fflush(stdout);
		/* the calls it was in, and its temporaries, are freed without
		 * running destructors, as for any panic. (Their frames were on the
		 * C stack the panic jumped out of, so they're gone: their cells
		 * are in the interpreter's own stack of cells, every one zeroed
		 * when it was made, so all of those are freed from there.) */
		for (CellChunk *c = I->chunk; c; c = c->prev)
			for (int64_t i = 0; i < c->used; i++)
				value_free(&c->cells[i]);
		cells_reset(I);
		for (int i = 0; i < I->ntemps; i++) {
			value_free(&I->temps[i]->cell);
			rt_free(I->temps[i]);
		}
		I->depth = 0;
		I->ntemps = 0;
		I->goto_target = NULL;
		I->frame = &IS->main;
		/* The input is dropped from the session, so its own variables go;
		 * an earlier variable it moved from is given back a zero value. */
		for (int i = IS->committed; i < n; i++)
			value_free(&IS->main.cells[i]);
		for (int i = 0; i < IS->committed; i++)
			if (IS->before[i] != V_UNINIT && IS->main.cells[i].kind == V_UNINIT) {
				Symbol *sym = S->main_fn->frame.items[i];
				IS->main.cells[i] = zero_value(sym->type);
				fprintf(stderr, "note: '%s' was moved before the %s, so it has been reset to its zero value\n",
					sym->name, why == 2 ? "interruption" : "panic");
			}
		return why == 2 ? IRUN_INTERRUPTED : IRUN_PANIC;
	}

	IRun result = IRUN_OK;
	for (int i = first; i < body->stmts.len; i++) {
		int mark = I->ntemps;
		Exec ex = exec_stmt(I, body->stmts.items[i]);
		release_temps(I, mark);
		if (ex == EX_GOTO && I->goto_target->as.label.block == body) {
			i = I->goto_target->as.label.index;
			I->goto_target = NULL;
			continue;
		}
		if (ex == EX_RETURN) {
			Value r = IS->main.ret;
			*status = r.kind == V_INT ? (int)(r.as.i & 0xFF) : 0;
			result = IRUN_RETURNED;
			break;
		}
	}
	fflush(stdout);
	g_session_jmp = NULL;
	rt_panic_jmp = NULL;
	IS->committed = n;
	return result;
}

void isession_end(InterpSession *IS)
{
	Interp *I = &IS->I;
	if (IS->S) {
		rt_panic_jmp = &IS->jb;
		if (setjmp(IS->jb) == 0) {
			I->frame = &IS->main;
			drop_locals(I, IS->S->main_fn->body);   /* destructors run, as at the end of main */
			for (int i = 0; i < IS->ngtab; i++)
				value_drop(I, IS->gtab[i].cell);
		}
		release_temps(I, 0);
		rt_panic_jmp = NULL;
		fflush(stdout);
	}
	report_leaks();
	for (int i = 0; i < IS->ngtab; i++) {
		free(IS->gtab[i].key);
		rt_free(IS->gtab[i].cell);
	}
	free(IS->gtab);
	free(IS->before);
	free(I->temps);
	rt_free(I->globals);
	arena_free(&I->lits);
	free(IS->main.cells);
	interp_free_pools(&IS->I);
	free(IS);
	rt_panic_hook = NULL;
	rt_panic_map = NULL;
}
