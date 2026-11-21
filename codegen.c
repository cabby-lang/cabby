/* codegen.c — see codegen.h. */
#define _GNU_SOURCE
#include "codegen.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>

#include "diag.h"
#include "runtime/rt.h"

/* ============================================================== state */

typedef struct { char s[112]; } Op;

typedef struct {
	char slot[112];
	char flag[112];  /* empty: no drop needed */
	Type *ty;
} CTemp;

enum { SC_BLOCK, SC_LOOP, SC_SWITCH, SC_FN };

typedef struct {
	int kind;
	Block *block;
	SymbolVec *locals;
	char brk[112], cont[112];
} CScope;

typedef struct {
	const char *data;
	size_t len;
	int id;
} StrLit;

typedef struct {
	Stmt *stmt;
	char name[32];
} LabelName;

typedef struct CG {
	Sema *S;
	Arena A;

	char *types_buf, *glob_buf, *decl_buf, *func_buf;
	size_t types_len, glob_len, decl_len, func_len;
	FILE *types, *glob, *decl, *funcs;

	/* per function */
	char *entry_buf, *body_buf;
	size_t entry_len, body_len;
	FILE *entry, *body;
	FnDecl *fn;
	char **slot;          /* per frame index: alloca */
	char **flag;          /* per frame index: drop flag alloca, or NULL */
	bool term;            /* current basic block already terminated */
	CScope scopes[512];
	int nscopes;
	VEC(CTemp) temps;
	VEC(LabelName) labels;
	char sret[16];        /* "" if the function returns a scalar/void */
	int ntmp, nlbl;

	VEC(StrLit) strs;
	VEC(const char *) declared;   /* extra declarations already emitted */
	VEC(Type *) lists;            /* element types of the lists used: helpers ldrop.N, lcopy.N, lfind.N */
	VEC(Type *) lookups;          /* the lookup types used: helpers kdrop.N, kclear.N, kcopy.N ... */
	VEC(Type *) fdrops;           /* the types a frame may drop: helpers fdrop.N */
	VEC(Expr *) sorts;            /* L.sort_by / sort_with calls: helpers sbefore.N */

	/* `... else`: what a function has that a caught panic must drop (each
	 * local and temporary with a drop flag), told to the runtime when the
	 * function starts while an else is waiting (cby_trying) */
	bool frames_on;               /* in a program's function (not a helper) */
	VEC(CTemp) fitems;
	int try_depth;                /* inside an else's statement: flags are written volatile */
	bool keep_released;           /* the else's statement's temporaries, kept for its catch */
	VEC(CTemp) released;
} CG;

static void out(FILE *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void out(FILE *f, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
}

static const char *astr(CG *G, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static const char *astr(CG *G, const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (n < 0)
		n = 0;
	if ((size_t)n >= sizeof buf)
		n = sizeof buf - 1;
	return arena_strndup(&G->A, buf, (size_t)n);
}

static Op op(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static Op op(const char *fmt, ...)
{
	Op o;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(o.s, sizeof o.s, fmt, ap);
	va_end(ap);
	return o;
}

static Op tmp(CG *G) { return op("%%t%d", ++G->ntmp); }
static Op lbl(CG *G, const char *what) { return op("L%d.%s", ++G->nlbl, what); }

/* Instructions go to the body; after a terminator, a fresh (unreachable)
 * block is opened so that dead code still forms valid IR. */
static void emit(CG *G, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void emit(CG *G, const char *fmt, ...)
{
	if (G->term) {
		fprintf(G->body, "L%d.dead:\n", ++G->nlbl);
		G->term = false;
	}
	va_list ap;
	va_start(ap, fmt);
	fputc('\t', G->body);
	vfprintf(G->body, fmt, ap);
	fputc('\n', G->body);
	va_end(ap);
}

static void emit_term(CG *G, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void emit_term(CG *G, const char *fmt, ...)
{
	if (G->term) {
		fprintf(G->body, "L%d.dead:\n", ++G->nlbl);
		G->term = false;
	}
	va_list ap;
	va_start(ap, fmt);
	fputc('\t', G->body);
	vfprintf(G->body, fmt, ap);
	fputc('\n', G->body);
	va_end(ap);
	G->term = true;
}

static void place_label(CG *G, Op l)
{
	if (!G->term)
		fprintf(G->body, "\tbr label %%%s\n", l.s);
	fprintf(G->body, "%s:\n", l.s);
	G->term = false;
}

static void br(CG *G, Op l) { emit_term(G, "br label %%%s", l.s); }

/* ============================================================== types */

static bool is_obj(const Type *t) { return t->kind == TY_OBJECT && t->obj->kind == OBJ_OBJECT; }

static bool is_agg(const Type *t)
{
	switch (t->kind) {
	case TY_STR: case TY_SLICE: case TY_ARRAY: case TY_LIST: case TY_LOOKUP: case TY_FILE:
		return true;
	case TY_OBJECT:
		return t->obj->kind != OBJ_OBJECT;
	default:
		return false;
	}
}

/* --wasm: for WebAssembly (wasm32-wasip1), where a pointer, a size and a C
 * long are 32 bits (a Cabby long is still 64) */
static bool codegen_wasm;

static const char *llty(CG *G, const Type *t)
{
	switch (t->kind) {
	case TY_VOID:   return "void";
	case TY_BOOL:   return "i8";
	case TY_INT:    return t->bits == 8 ? "i8" : t->bits == 16 ? "i16" : t->bits == 32 ? "i32" : "i64";
	case TY_FLOAT:  return t->bits == 32 ? "float" : "double";
	case TY_STR:    return "%str";
	case TY_LIST:   return "%list";
	case TY_LOOKUP: return "%lookup";
	case TY_FILE:   return "%file";
	case TY_SLICE:  return "%slice";
	case TY_REF:    return "ptr";
	case TY_HANDLER: return "{ ptr, ptr }";   /* the window, its method */
	case TY_ADDRESS: return "ptr";
	case TY_CALLBACK: return "ptr";   /* a C function pointer */
	case TY_LINK:   return "i64";     /* a place in rt.c's table, and its generation */
	case TY_ARRAY:  return astr(G, "[%" PRIu64 " x %s]", t->len, llty(G, t->inner));
	case TY_OBJECT:
		switch (t->obj->kind) {
		case OBJ_STRUCT: return astr(G, "%%S.%d", t->obj->id);
		case OBJ_UNION:  return astr(G, "%%U.%d", t->obj->id);
		default:         return "ptr";
		}
	default:
		return "i8";
	}
}

/* SSA (register) type. */
static const char *vty(CG *G, const Type *t) { return t->kind == TY_BOOL ? "i1" : llty(G, t); }

static uint64_t size_of(const Type *t)
{
	return t->kind == TY_OBJECT && t->obj->kind == OBJ_OBJECT ? (uint64_t)type_ptr_size : type_size(t);
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

static Op fconst(double v, int bits)
{
	if (bits == 32)
		v = (double)(float)v;
	uint64_t u;
	memcpy(&u, &v, 8);
	return op("0x%016" PRIX64, u);
}

static Op iconst(const Type *t, int64_t v)
{
	if (t->kind == TY_BOOL)
		return op("%s", v ? "true" : "false");
	return op("%" PRId64, norm(v, t));
}

/* ============================================================== module pieces */

static bool mark_declared(CG *G, const char *name)
{
	for (int i = 0; i < G->declared.len; i++)
		if (strcmp(G->declared.items[i], name) == 0)
			return false;
	vec_push(&G->A, G->declared, arena_strndup(&G->A, name, strlen(name)));
	return true;
}

static void need_intrinsic(CG *G, const char *name, const char *decl)
{
	if (mark_declared(G, name))
		out(G->decl, "%s\n", decl);
}

static int file_const(CG *G, int file)
{
	char key[32];
	snprintf(key, sizeof key, "file.%d", file);
	if (mark_declared(G, key)) {
		const char *n = diag_file_name(file);
		out(G->glob, "@.file.%d = private unnamed_addr constant [%zu x i8] c\"", file, strlen(n) + 1);
		for (const char *p = n; *p; p++) {
			unsigned char c = (unsigned char)*p;
			if (c < 0x20 || c > 0x7E || c == '"' || c == '\\')
				out(G->glob, "\\%02X", c);
			else
				fputc(c, G->glob);
		}
		out(G->glob, "\\00\"\n");
	}
	return file;
}

static Op file_ref(CG *G, SrcLoc l) { return op("@.file.%d", file_const(G, l.file < 0 ? 0 : l.file)); }

static int string_literal_id(CG *G, const char *data, size_t len)
{
	for (int i = 0; i < G->strs.len; i++)
		if (G->strs.items[i].len == len && memcmp(G->strs.items[i].data, data, len) == 0)
			return G->strs.items[i].id;
	int id = G->strs.len;
	StrLit s = { data, len, id };
	vec_push(&G->A, G->strs, s);
	out(G->glob, "@.str.%d = private unnamed_addr constant [%zu x i8] c\"", id, len + 1);
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)data[i];
		if (c < 0x20 || c > 0x7E || c == '"' || c == '\\')
			out(G->glob, "\\%02X", c);
		else
			fputc(c, G->glob);
	}
	out(G->glob, "\\00\"\n");
	return id;
}

#define STR_STATIC_BIT ((int64_t)1 << 62)

static const char *c_name(const FnDecl *fn);

static const char *fn_name(CG *G, FnDecl *fn)
{
	if (fn->definition)
		fn = fn->definition;
	if (fn->is_extern)   /* (C's printf is Cabby's, which follows stderr ...) */
		return astr(G, "@%s", c_name(fn));
	if (fn->owner) {
		if (fn->is_destructor)
			return astr(G, "@\"cby.%s.~\"", fn->owner->name);
		if (fn->is_maker)
			return astr(G, "@\"cby.%s.+\"", fn->owner->name);
		return astr(G, "@\"cby.%s.%s\"", fn->owner->name, fn->name);
	}
	if (fn->is_static)
		return astr(G, "@\"cby.%s.%d\"", fn->name, fn->loc.file);
	return astr(G, "@\"cby.%s\"", fn->name);
}

static const char *global_name(CG *G, Symbol *s)
{
	if (s->is_static_local)
		return astr(G, "@\"g.%d.%s\"", s->index, s->name);
	return astr(G, "@\"g.%s.%d\"", s->name, s->index);
}

/* ============================================================== locals & flags */

static Op entry_alloca(CG *G, const char *ty, const char *hint)
{
	Op o = op("%%%s.%d", hint, ++G->ntmp);
	out(G->entry, "\t%s = alloca %s\n", o.s, ty);
	return o;
}

static Op new_flag(CG *G)
{
	Op f = entry_alloca(G, "i8", "flag");
	out(G->entry, "\tstore i8 0, ptr %s\n", f.s);
	return f;
}

static void set_flag(CG *G, const char *flag, int v)
{
	if (flag && flag[0])
		emit(G, "store %si8 %d, ptr %s", G->try_depth ? "volatile " : "", v, flag);
}

/* ============================================================== drop & zero */

static void emit_drop(CG *G, Type *t, Op ptr);
static void emit_zero(CG *G, Type *t, Op ptr, bool already_zero);

/* The helpers' number for lists of `elem` (registered on first use). */
static int list_id(CG *G, Type *elem)
{
	for (int i = 0; i < G->lists.len; i++)
		if (type_eq(G->lists.items[i], elem))
			return i;
	vec_push(&G->A, G->lists, elem);
	return G->lists.len - 1;
}

/* The helpers' number for a lookup type (registered on first use). */
static int lookup_id(CG *G, Type *lt)
{
	for (int i = 0; i < G->lookups.len; i++)
		if (type_eq(G->lookups.items[i], lt))
			return i;
	vec_push(&G->A, G->lookups, lt);
	return G->lookups.len - 1;
}

/* How the runtime compares and shows a lookup's keys (RT_KEY_*). */
static int key_kind(const Type *k)
{
	if (k->kind == TY_STR)
		return RT_KEY_STR;
	if (k->kind == TY_BOOL)
		return RT_KEY_BOOL;
	if (k->is_char)
		return RT_KEY_CHAR;
	return k->is_signed ? RT_KEY_INT : RT_KEY_UINT;
}

/* An element's size in bytes, as LLVM lays it out. */
static const char *elem_size(CG *G, Type *elem)
{
	return astr(G, "ptrtoint (ptr getelementptr (%s, ptr null, i32 1) to i64)", llty(G, elem));
}

static void memcpy_(CG *G, Op dst, Op src, uint64_t n)
{
	need_intrinsic(G, "llvm.memcpy", "declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)");
	emit(G, "call void @llvm.memcpy.p0.p0.i64(ptr %s, ptr %s, i64 %" PRIu64 ", i1 false)", dst.s, src.s, n);
}

static void memset0(CG *G, Op dst, uint64_t n)
{
	need_intrinsic(G, "llvm.memset", "declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)");
	emit(G, "call void @llvm.memset.p0.i64(ptr %s, i8 0, i64 %" PRIu64 ", i1 false)", dst.s, n);
}

static bool contains_object(const Type *t)
{
	switch (t->kind) {
	case TY_ARRAY:
		return contains_object(t->inner);
	case TY_OBJECT:
		if (t->obj->kind == OBJ_OBJECT)
			return true;
		for (int i = 0; i < t->obj->field_count; i++)
			if (contains_object(t->obj->fields[i].type))
				return true;
		return false;
	default:
		return false;
	}
}

/* Could dropping a value of this kind lead to dropping another list or
 * lookup (and so, perhaps, a chain of them)? */
static bool may_nest(const Type *t)
{
	switch (t->kind) {
	case TY_LIST:
	case TY_LOOKUP:
		return true;
	case TY_ARRAY:
		return may_nest(t->inner);
	case TY_OBJECT:
		if (t->obj->kind == OBJ_OBJECT)
			return true;
		for (int i = 0; i < t->obj->field_count; i++)
			if (may_nest(t->obj->fields[i].type))
				return true;
		return false;
	default:
		return false;
	}
}

/* Static (exact-class) pointer to field `slot` of an object block. */
static Op obj_field_static(CG *G, ObjectInfo *K, Op handle, int slot)
{
	FieldInfo *f = &K->fields[slot];
	int block = -1;
	for (int i = 0; i < K->layout_len; i++)
		if (K->layout[i] == f->owner)
			block = i;
	Op r = tmp(G);
	emit(G, "%s = getelementptr %%O.%d, ptr %s, i32 0, i32 %d, i32 %d", r.s, K->id, handle.s, block + 1, f->own_index);
	return r;
}

static void array_loop(CG *G, Type *t, Op ptr, void (*body)(CG *, Type *, Op, bool), bool arg)
{
	Op idx = entry_alloca(G, "i64", "i");
	Op head = lbl(G, "arr.head"), loop = lbl(G, "arr.body"), done = lbl(G, "arr.done");
	emit(G, "store i64 0, ptr %s", idx.s);
	br(G, head);
	place_label(G, head);
	Op i = tmp(G), c = tmp(G);
	emit(G, "%s = load i64, ptr %s", i.s, idx.s);
	emit(G, "%s = icmp ult i64 %s, %" PRIu64, c.s, i.s, t->len);
	emit_term(G, "br i1 %s, label %%%s, label %%%s", c.s, loop.s, done.s);
	place_label(G, loop);
	Op e = tmp(G);
	emit(G, "%s = getelementptr %s, ptr %s, i64 0, i64 %s", e.s, llty(G, t), ptr.s, i.s);
	body(G, t->inner, e, arg);
	Op n = tmp(G);
	emit(G, "%s = add i64 %s, 1", n.s, i.s);
	emit(G, "store i64 %s, ptr %s", n.s, idx.s);
	br(G, head);
	place_label(G, done);
}

static void drop_body(CG *G, Type *t, Op p, bool unused) { (void)unused; emit_drop(G, t, p); }

static void emit_drop(CG *G, Type *t, Op ptr)
{
	if (!type_needs_drop(t))
		return;
	switch (t->kind) {
	case TY_STR:
		emit(G, "call void @cby_str_drop(ptr %s)", ptr.s);
		break;
	case TY_ARRAY:
		array_loop(G, t, ptr, drop_body, false);
		break;
	case TY_LIST:
		/* a list that may hold more lists (a chain of nodes) is dropped
		 * through the runtime, which keeps the nesting shallow */
		if (may_nest(t->inner))
			emit(G, "call void @cby_drop_nested(ptr %s, i64 %" PRIu64 ", ptr @\"ldrop.%d\")", ptr.s, type_size(t), list_id(G, t->inner));
		else
			emit(G, "call void @\"ldrop.%d\"(ptr %s)", list_id(G, t->inner), ptr.s);
		break;
	case TY_LOOKUP:
		if (may_nest(t->inner) || (t->key && may_nest(t->key)))
			emit(G, "call void @cby_drop_nested(ptr %s, i64 %" PRIu64 ", ptr @\"kdrop.%d\")", ptr.s, type_size(t), lookup_id(G, t));
		else
			emit(G, "call void @\"kdrop.%d\"(ptr %s)", lookup_id(G, t), ptr.s);
		break;
	case TY_FILE:
		emit(G, "call void @cby_file_drop(ptr %s)", ptr.s);
		break;
	case TY_OBJECT:
		if (t->obj->kind == OBJ_OBJECT && t->obj->has_derived) {
			/* it may be one made from it: dropped as what it is (its
			 * classinfo's drop) */
			Op h = tmp(G), ci = tmp(G), dp = tmp(G), d = tmp(G);
			emit(G, "%s = load ptr, ptr %s", h.s, ptr.s);
			emit(G, "%s = load ptr, ptr %s", ci.s, h.s);
			emit(G, "%s = getelementptr %%classinfo, ptr %s, i32 0, i32 3", dp.s, ci.s);
			emit(G, "%s = load ptr, ptr %s", d.s, dp.s);
			emit(G, "call void %s(ptr %s)", d.s, h.s);
		} else if (t->obj->kind == OBJ_OBJECT) {
			Op h = tmp(G);
			emit(G, "%s = load ptr, ptr %s", h.s, ptr.s);
			emit(G, "call void @\"drop.%d\"(ptr %s)", t->obj->id, h.s);
		} else {
			for (int i = 0; i < t->obj->field_count; i++) {
				Type *ft = t->obj->fields[i].type;
				if (!type_needs_drop(ft))
					continue;
				Op f = tmp(G);
				emit(G, "%s = getelementptr %%S.%d, ptr %s, i32 0, i32 %d", f.s, t->obj->id, ptr.s, i);
				emit_drop(G, ft, f);
			}
		}
		break;
	default:
		break;
	}
}

static void zero_body(CG *G, Type *t, Op p, bool already) { emit_zero(G, t, p, already); }

/* Writes the zero value of `t` to ptr (a str is { NULL, 0 }; objects are
 * freshly allocated with zeroed fields). */
static void emit_zero(CG *G, Type *t, Op ptr, bool already_zero)
{
	if (!already_zero)
		memset0(G, ptr, size_of(t));
	if (!contains_object(t))
		return;
	switch (t->kind) {
	case TY_ARRAY:
		array_loop(G, t, ptr, zero_body, true);
		break;
	case TY_OBJECT:
		if (t->obj->kind == OBJ_OBJECT) {
			Op h = tmp(G);
			emit(G, "%s = call ptr @\"new.%d\"()", h.s, t->obj->id);
			emit(G, "store ptr %s, ptr %s", h.s, ptr.s);
		} else {
			for (int i = 0; i < t->obj->field_count; i++) {
				Type *ft = t->obj->fields[i].type;
				if (!contains_object(ft))
					continue;
				Op f = tmp(G);
				emit(G, "%s = getelementptr %%S.%d, ptr %s, i32 0, i32 %d", f.s, t->obj->id, ptr.s, i);
				emit_zero(G, ft, f, true);
			}
		}
		break;
	default:
		break;
	}
}

/* ============================================================== scopes & temps */

static void drop_symbol(CG *G, Symbol *s)
{
	if (s->kind == SYM_GLOBAL || !type_needs_drop(s->type) || s->type->kind == TY_REF)
		return;
	const char *flag = G->flag[s->index];
	Op yes = lbl(G, "drop"), no = lbl(G, "kept");
	Op v = tmp(G), c = tmp(G);
	emit(G, "%s = load i8, ptr %s", v.s, flag);
	emit(G, "%s = icmp ne i8 %s, 0", c.s, v.s);
	emit_term(G, "br i1 %s, label %%%s, label %%%s", c.s, yes.s, no.s);
	place_label(G, yes);
	emit_drop(G, s->type, op("%s", G->slot[s->index]));
	emit(G, "store i8 0, ptr %s", flag);
	br(G, no);
	place_label(G, no);
}

static void cleanup_scope(CG *G, CScope *sc)
{
	if (!sc->locals)
		return;
	for (int i = sc->locals->len - 1; i >= 0; i--)
		drop_symbol(G, sc->locals->items[i]);
}

/* Drops every scope above `keep` (scopes[keep..] are left). */
static void cleanup_to(CG *G, int keep)
{
	for (int i = G->nscopes - 1; i >= keep; i--)
		cleanup_scope(G, &G->scopes[i]);
}

static CScope *push_scope(CG *G, int kind, Block *b, SymbolVec *locals)
{
	CScope *sc = &G->scopes[G->nscopes++];
	memset(sc, 0, sizeof *sc);
	sc->kind = kind;
	sc->block = b;
	sc->locals = locals;
	return sc;
}

static Op new_temp_slot(CG *G, Type *t, bool needs_drop)
{
	CTemp ct = { .ty = t };
	Op s = entry_alloca(G, llty(G, t), "tmp");
	snprintf(ct.slot, sizeof ct.slot, "%s", s.s);
	if (needs_drop && type_needs_drop(t)) {
		Op f = new_flag(G);
		snprintf(ct.flag, sizeof ct.flag, "%s", f.s);
		if (G->frames_on)
			vec_push(&G->A, G->fitems, ct);
	}
	vec_push(&G->A, G->temps, ct);
	return s;
}

static void temp_initialized(CG *G, Op slot)
{
	for (int i = G->temps.len - 1; i >= 0; i--)
		if (strcmp(G->temps.items[i].slot, slot.s) == 0) {
			set_flag(G, G->temps.items[i].flag, 1);
			return;
		}
}

static void release_temps(CG *G, int mark)
{
	while (G->temps.len > mark) {
		CTemp *t = &G->temps.items[--G->temps.len];
		if (G->keep_released)
			vec_push(&G->A, G->released, *t);
		if (!t->flag[0])
			continue;
		Op yes = lbl(G, "tdrop"), no = lbl(G, "tkept");
		Op v = tmp(G), c = tmp(G);
		emit(G, "%s = load %si8, ptr %s", v.s, G->try_depth ? "volatile " : "", t->flag);
		emit(G, "%s = icmp ne i8 %s, 0", c.s, v.s);
		emit_term(G, "br i1 %s, label %%%s, label %%%s", c.s, yes.s, no.s);
		place_label(G, yes);
		emit_drop(G, t->ty, op("%s", t->slot));
		emit(G, "store i8 0, ptr %s", t->flag);
		br(G, no);
		place_label(G, no);
	}
}

/* ============================================================== loads & stores */

static Op load(CG *G, Type *t, Op ptr, bool is_volatile)
{
	Op r = tmp(G);
	if (t->kind == TY_BOOL) {
		Op b = tmp(G);
		emit(G, "%s = load %si8, ptr %s", b.s, is_volatile ? "volatile " : "", ptr.s);
		emit(G, "%s = icmp ne i8 %s, 0", r.s, b.s);
		return r;
	}
	emit(G, "%s = load %s%s, ptr %s", r.s, is_volatile ? "volatile " : "", llty(G, t), ptr.s);
	return r;
}

static void store(CG *G, Type *t, Op v, Op ptr, bool is_volatile)
{
	if (t->kind == TY_BOOL) {
		Op b = tmp(G);
		emit(G, "%s = zext i1 %s to i8", b.s, v.s);
		emit(G, "store %si8 %s, ptr %s", is_volatile ? "volatile " : "", b.s, ptr.s);
		return;
	}
	emit(G, "store %s%s %s, ptr %s", is_volatile ? "volatile " : "", llty(G, t), v.s, ptr.s);
}

/* ============================================================== expressions */

static Op gen_val(CG *G, Expr *e);
static void gen_obj_alloc(CG *G, Op h, int id);
static void declare_extern(CG *G, FnDecl *fn);
static void gen_into(CG *G, Expr *e, Op dst);
static Op gen_place(CG *G, Expr *e);
static Op gen_call(CG *G, Expr *e, Op *sret);

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

static bool through_ref(const Expr *e)
{
	return (e->kind == EXPR_IDENT || e->kind == EXPR_SELF) && e->as.ident.sym &&
	       (e->as.ident.sym->kind == SYM_SELF || e->as.ident.sym->type->kind == TY_REF);
}

/* Pointer to the value of `e`: its place, or a statement temporary. */
static Op gen_read(CG *G, Expr *e)
{
	if (is_place(e))
		return gen_place(G, e);
	bool literal = e->kind == EXPR_STRING && !e->to_owned_str;
	Op s = new_temp_slot(G, e->ty, !literal);
	gen_into(G, e, s);
	temp_initialized(G, s);
	return s;
}

static Op symbol_storage(CG *G, Symbol *s)
{
	if (s->kind == SYM_GLOBAL)
		return op("%s", global_name(G, s));
	return op("%s", G->slot[s->index]);
}

/* ---- lookups: { keys, vals, dead, index, n, len, cap, icap } ---- */

static Op str_operand(CG *G, Expr *a);

static Op lk_field(CG *G, Op l, int which)
{
	Op p = tmp(G), v = tmp(G);
	emit(G, "%s = getelementptr %%lookup, ptr %s, i32 0, i32 %d", p.s, l.s, which);
	emit(G, "%s = load %s, ptr %s", v.s, which >= 4 ? "i64" : "ptr", p.s);
	return v;
}

/* The key (part 1) or value (part 2) at a position */
static Op lk_at(CG *G, Type *lt, Op l, Op pos, int part)
{
	Op base = lk_field(G, l, part == 1 ? 0 : 1), r = tmp(G);
	emit(G, "%s = getelementptr %s, ptr %s, i64 %s", r.s, llty(G, part == 1 ? lt->key : lt->inner), base.s, pos.s);
	return r;
}

/* A key, in memory, for the runtime to read */
static Op lk_key(CG *G, Expr *k, Type *kt)
{
	if (kt->kind == TY_STR)
		return str_operand(G, k);
	Op v = gen_val(G, k);
	Op p = entry_alloca(G, llty(G, kt), "key");
	store(G, kt, v, p, false);
	return p;
}

/* Finding a whole-number key, written into the program (as a table written
 * for the one kind would be) rather than called in the runtime: the same
 * hash (Fibonacci, as rt_hash_u64) and the same index. One function for
 * each size of key, which LLVM inlines where it's used. */
static const char *lk_find_fn(CG *G, int bits)
{
	char name[32];
	snprintf(name, sizeof name, "cby.lk_find.i%d", bits);
	if (mark_declared(G, name)) {
		need_intrinsic(G, "llvm.fshl.i64", "declare i64 @llvm.fshl.i64(i64, i64, i64)");
		out(G->decl,
		    "define internal i64 @\"%s\"(ptr %%l, i%d %%k) alwaysinline {\n"
		    "entry:\n"
		    "\t%%lenp = getelementptr %%lookup, ptr %%l, i32 0, i32 5\n"
		    "\t%%len = load i64, ptr %%lenp\n"
		    "\t%%none_in = icmp eq i64 %%len, 0\n"
		    "\tbr i1 %%none_in, label %%none, label %%go\n"
		    "go:\n"
		    "\t%%keys = load ptr, ptr %%l\n"
		    "\t%%ip = getelementptr %%lookup, ptr %%l, i32 0, i32 3\n"
		    "\t%%index = load ptr, ptr %%ip\n"
		    "\t%%cp = getelementptr %%lookup, ptr %%l, i32 0, i32 7\n"
		    "\t%%icap = load i64, ptr %%cp\n"
		    "\t%%mask = sub i64 %%icap, 1\n"
		    "\t%%x = %s\n"
		    "\t%%m = mul i64 %%x, -7046029254386353131\n"
		    "\t%%h = call i64 @llvm.fshl.i64(i64 %%m, i64 %%m, i64 32)\n"
		    "\t%%i0 = and i64 %%h, %%mask\n"
		    "\tbr label %%probe\n"
		    "probe:\n"
		    "\t%%i = phi i64 [ %%i0, %%go ], [ %%i2, %%step ]\n"
		    "\t%%sp = getelementptr i32, ptr %%index, i64 %%i\n"
		    "\t%%s = load i32, ptr %%sp\n"
		    "\t%%used = icmp sgt i32 %%s, 0\n"
		    "\tbr i1 %%used, label %%cmp, label %%end\n"
		    "cmp:\n"
		    "\t%%s64 = zext i32 %%s to i64\n"
		    "\t%%pos = sub i64 %%s64, 1\n"
		    "\t%%ep = getelementptr i%d, ptr %%keys, i64 %%pos\n"
		    "\t%%e = load i%d, ptr %%ep\n"
		    "\t%%same = icmp eq i%d %%e, %%k\n"
		    "\tbr i1 %%same, label %%found, label %%step\n"
		    "end:\n"
		    "\t%%empty = icmp eq i32 %%s, 0\n"
		    "\tbr i1 %%empty, label %%none, label %%step\n"
		    "step:\n"
		    "\t%%i1 = add i64 %%i, 1\n"
		    "\t%%i2 = and i64 %%i1, %%mask\n"
		    "\tbr label %%probe\n"
		    "found:\n"
		    "\tret i64 %%pos\n"
		    "none:\n"
		    "\tret i64 -1\n"
		    "}\n",
		    name, bits, bits == 64 ? "add i64 %k, 0" : astr(G, "zext i%d %%k to i64", bits), bits, bits, bits);
	}
	return astr(G, "@\"%s\"", name);
}

/* The position of a whole-number key, or -1, found in place; k is where
 * the key is (lk_key). */
static Op lk_find_number(CG *G, Type *kt, Op l, Op k)
{
	int bits = (int)size_of(kt) * 8;
	const char *fn = lk_find_fn(G, bits);
	Op v = tmp(G), pos = tmp(G);
	emit(G, "%s = load i%d, ptr %s", v.s, bits, k.s);
	emit(G, "%s = call i64 %s(ptr %s, i%d %s)", pos.s, fn, l.s, bits, v.s);
	return pos;
}

/* The key's position, or -1 */
static Op lk_find(CG *G, Type *lt, Op l, Op k)
{
	if (lt->key->kind != TY_STR)
		return lk_find_number(G, lt->key, l, k);
	Op pos = tmp(G);
	emit(G, "%s = call i64 @cby_lookup_find(ptr %s, i32 %d, i64 %s, ptr %s)", pos.s, l.s, key_kind(lt->key),
	     elem_size(G, lt->key), k.s);
	return pos;
}

/* The key's position, where it must be: a panic, saying so, if it isn't. */
static Op lk_must_find(CG *G, Type *lt, Op l, Op k, SrcLoc loc)
{
	Op f = file_ref(G, loc);
	if (lt->key->kind == TY_STR) {
		Op pos = tmp(G);
		emit(G, "%s = call i64 @cby_lookup_at(ptr %s, i32 %d, i64 %s, ptr %s, ptr %s, i32 %d, i32 %d)", pos.s,
		     l.s, key_kind(lt->key), elem_size(G, lt->key), k.s, f.s, loc.line, loc.col);
		return pos;
	}
	Op found = lk_find_number(G, lt->key, l, k), c = tmp(G), pos = tmp(G);
	Op ok = lbl(G, "k.found"), bad = lbl(G, "k.missing");
	emit(G, "%s = icmp slt i64 %s, 0", c.s, found.s);
	emit_term(G, "br i1 %s, label %%%s, label %%%s, !prof !0", c.s, bad.s, ok.s);
	place_label(G, bad);   /* (the runtime says which key) */
	emit(G, "%s = call i64 @cby_lookup_at(ptr %s, i32 %d, i64 %s, ptr %s, ptr %s, i32 %d, i32 %d)", pos.s, l.s,
	     key_kind(lt->key), elem_size(G, lt->key), k.s, f.s, loc.line, loc.col);
	emit_term(G, "unreachable");
	place_label(G, ok);
	return found;
}

/* Lookup[key] = the value at src (moved in): a new key is added; an old
 * value is dropped first. */
static void lk_store(CG *G, Type *lt, Op l, Expr *key, Op src)
{
	Op k = lk_key(G, key, lt->key);
	Op isnew = entry_alloca(G, "i8", "new"), pos = tmp(G);
	if (lt->key->kind != TY_STR) {
		/* there already (found in place), or added by the runtime */
		Op found = lk_find_number(G, lt->key, l, k), c = tmp(G), added = tmp(G);
		Op hit = lbl(G, "k.hit"), miss = lbl(G, "k.add"), join = lbl(G, "k.join");
		emit(G, "%s = icmp sge i64 %s, 0", c.s, found.s);
		emit_term(G, "br i1 %s, label %%%s, label %%%s", c.s, hit.s, miss.s);
		place_label(G, hit);
		emit(G, "store i8 0, ptr %s", isnew.s);
		br(G, join);
		place_label(G, miss);
		emit(G, "%s = call i64 @cby_lookup_put(ptr %s, i32 %d, i64 %s, i64 %s, ptr %s, ptr %s)", added.s, l.s,
		     key_kind(lt->key), elem_size(G, lt->key), elem_size(G, lt->inner), k.s, isnew.s);
		br(G, join);
		place_label(G, join);
		emit(G, "%s = phi i64 [ %s, %%%s ], [ %s, %%%s ]", pos.s, found.s, hit.s, added.s, miss.s);
	} else {
		emit(G, "%s = call i64 @cby_lookup_put(ptr %s, i32 %d, i64 %s, i64 %s, ptr %s, ptr %s)", pos.s, l.s,
		     key_kind(lt->key), elem_size(G, lt->key), elem_size(G, lt->inner), k.s, isnew.s);
	}
	Op slot = lk_at(G, lt, l, pos, 2);
	if (type_needs_drop(lt->inner)) {
		Op b = tmp(G), c = tmp(G), old = lbl(G, "k.old"), put = lbl(G, "k.put");
		emit(G, "%s = load i8, ptr %s", b.s, isnew.s);
		emit(G, "%s = icmp eq i8 %s, 0", c.s, b.s);
		emit_term(G, "br i1 %s, label %%%s, label %%%s", c.s, old.s, put.s);
		place_label(G, old);
		emit_drop(G, lt->inner, slot);
		br(G, put);
		place_label(G, put);
	}
	memcpy_(G, slot, src, size_of(lt->inner));
}

static Op gen_place(CG *G, Expr *e)
{
	switch (e->kind) {
	case EXPR_IDENT:
	case EXPR_SELF: {
		Symbol *s = e->as.ident.sym;
		Op st = symbol_storage(G, s);
		if (s->type->kind == TY_REF && s->kind != SYM_SELF) {
			Op r = tmp(G);
			emit(G, "%s = load ptr, ptr %s", r.s, st.s);
			return r;
		}
		return st;   /* self's slot holds the object handle, like an object variable */
	}
	case EXPR_FIELD: {
		Expr *base = e->as.field.object;
		Type *bt = base->ty;
		Op bp = gen_read(G, base);
		ObjectInfo *o = bt->obj;
		if (o->kind == OBJ_UNION)
			return bp;
		if (o->kind == OBJ_STRUCT) {
			int slot = o->block_start[e->as.field.owner_id] + e->as.field.field_index;
			Op r = tmp(G);
			emit(G, "%s = getelementptr %%S.%d, ptr %s, i32 0, i32 %d", r.s, o->id, bp.s, slot);
			return r;
		}
		Op h = tmp(G);
		emit(G, "%s = load ptr, ptr %s", h.s, bp.s);
		if (e->as.field.worker_check)   /* (a worker's, from outside it: not while it works) */
			emit(G, "call void @rt_worker_check(ptr %s, ptr %s, i32 %d, i32 %d)", h.s, file_ref(G, e->loc).s,
			     e->loc.line, e->loc.col);
		if (!through_ref(base) && !o->has_derived) {
			int slot = o->block_start[e->as.field.owner_id] + e->as.field.field_index;
			return obj_field_static(G, o, h, slot);
		}
		/* dynamic class: classinfo->offsets[owner] locates the owner's block */
		int owner = e->as.field.owner_id;
		Op ci = tmp(G), offs = tmp(G), op_ = tmp(G), off = tmp(G), blk = tmp(G), r = tmp(G);
		emit(G, "%s = load ptr, ptr %s", ci.s, h.s);
		emit(G, "%s = getelementptr %%classinfo, ptr %s, i32 0, i32 1", offs.s, ci.s);
		emit(G, "%s = load ptr, ptr %s", op_.s, offs.s);
		Op slotp = tmp(G);
		emit(G, "%s = getelementptr i64, ptr %s, i64 %d", slotp.s, op_.s, owner);
		emit(G, "%s = load i64, ptr %s", off.s, slotp.s);
		emit(G, "%s = getelementptr i8, ptr %s, i64 %s", blk.s, h.s, off.s);
		emit(G, "%s = getelementptr %%B.%d, ptr %s, i32 0, i32 %d", r.s, owner, blk.s, e->as.field.field_index);
		return r;
	}
	case EXPR_LINK_GET: {
		/* where the runtime keeps the object's handle: like an object
		 * variable's slot (a panic if it's gone) */
		Op l = gen_val(G, e->as.link.operand), r = tmp(G);
		const char *nm = e->as.link.name, *kind = e->ty->obj->name;
		int ni = string_literal_id(G, nm, strlen(nm)), ki = string_literal_id(G, kind, strlen(kind));
		emit(G, "%s = call ptr @cby_link_get(i64 %s, ptr @.str.%d, ptr @.str.%d, ptr %s, i32 %d, i32 %d)", r.s, l.s, ni,
		     ki, file_ref(G, e->loc).s, e->loc.line, e->loc.col);
		return r;
	}
	case EXPR_AT: {
		/* the address itself is the place; A[i] steps i values along first */
		Op a = gen_val(G, e->as.at.address);
		if (!e->as.at.step)
			return a;
		Op i = gen_val(G, e->as.at.step), r = tmp(G);
		emit(G, "%s = getelementptr %s, ptr %s, i64 %s", r.s, llty(G, e->ty), a.s, i.s);
		return r;
	}
	case EXPR_INDEX: {
		Expr *base = e->as.index.base;
		Type *bt = base->ty;
		Op bp = gen_read(G, base);
		if (bt->kind == TY_LOOKUP) {
			if (e->as.index.lookup_part)
				return lk_at(G, bt, bp, gen_val(G, e->as.index.index), e->as.index.lookup_part);
			Op k = lk_key(G, e->as.index.index, bt->key);
			return lk_at(G, bt, bp, lk_must_find(G, bt, bp, k, e->loc), 2);
		}
		Op idx = gen_val(G, e->as.index.index);
		Op len, data;
		if (bt->kind == TY_LIST) {
			Op dp = tmp(G), lp = tmp(G), d = tmp(G), n = tmp(G), c = tmp(G), bad = lbl(G, "loob"), ok = lbl(G, "linb");
			emit(G, "%s = getelementptr %%list, ptr %s, i32 0, i32 1", lp.s, bp.s);
			emit(G, "%s = load i64, ptr %s", n.s, lp.s);
			emit(G, "%s = icmp uge i64 %s, %s", c.s, idx.s, n.s);
			emit_term(G, "br i1 %s, label %%%s, label %%%s, !prof !0", c.s, bad.s, ok.s);
			place_label(G, bad);
			Op f = file_ref(G, e->loc);
			emit(G, "call void @rt_list_bounds(i64 %s, i64 %s, i1 false, ptr %s, i32 %d, i32 %d)", idx.s, n.s, f.s,
			     e->loc.line, e->loc.col);
			emit_term(G, "unreachable");
			place_label(G, ok);
			emit(G, "%s = getelementptr %%list, ptr %s, i32 0, i32 0", dp.s, bp.s);
			emit(G, "%s = load ptr, ptr %s", d.s, dp.s);
			Op r = tmp(G);
			emit(G, "%s = getelementptr %s, ptr %s, i64 %s", r.s, llty(G, bt->inner), d.s, idx.s);
			return r;
		}
		if (bt->kind == TY_SLICE) {
			Op dp = tmp(G), lp = tmp(G);
			data = tmp(G);
			len = tmp(G);
			emit(G, "%s = getelementptr %%slice, ptr %s, i32 0, i32 0", dp.s, bp.s);
			emit(G, "%s = load ptr, ptr %s", data.s, dp.s);
			emit(G, "%s = getelementptr %%slice, ptr %s, i32 0, i32 1", lp.s, bp.s);
			emit(G, "%s = load i64, ptr %s", len.s, lp.s);
		} else {
			data = bp;
			len = op("%" PRIu64, bt->len);
		}
		Op c = tmp(G), bad = lbl(G, "oob"), ok = lbl(G, "inb");
		emit(G, "%s = icmp uge i64 %s, %s", c.s, idx.s, len.s);
		emit_term(G, "br i1 %s, label %%%s, label %%%s, !prof !0", c.s, bad.s, ok.s);
		place_label(G, bad);
		Op f = file_ref(G, e->loc);
		emit(G, "call void @rt_bounds_check(i64 %s, i64 %s, ptr %s, i32 %d, i32 %d)", idx.s, len.s, f.s,
		     e->loc.line, e->loc.col);
		emit_term(G, "unreachable");
		place_label(G, ok);
		Op r = tmp(G);
		if (bt->kind == TY_SLICE)
			emit(G, "%s = getelementptr %s, ptr %s, i64 %s", r.s, llty(G, bt->inner), data.s, idx.s);
		else
			emit(G, "%s = getelementptr %s, ptr %s, i64 0, i64 %s", r.s, llty(G, bt), data.s, idx.s);
		return r;
	}
	default:
		break;
	}
	/* not a place: materialize */
	return gen_read(G, e);
}

static bool sym_volatile(const Expr *e)
{
	if (e->kind == EXPR_AT)
		return true;   /* memory at an address: a device register, perhaps */
	return (e->kind == EXPR_IDENT) && e->as.ident.sym && e->as.ident.sym->is_volatile;
}

/* Conversion between arithmetic types (C semantics, saturating float->int). */
static Op convert(CG *G, Op v, Type *from, Type *to)
{
	if (type_eq(from, to) || (from->kind == TY_OBJECT && to->kind == TY_OBJECT))
		return v;   /* (an object made from another, as that one: the same handle) */
	Op r = tmp(G);
	const char *ft = vty(G, from), *tt = vty(G, to);
	if (to->kind == TY_BOOL) {
		if (from->kind == TY_FLOAT)
			emit(G, "%s = fcmp une %s %s, 0.0", r.s, ft, v.s);
		else
			emit(G, "%s = icmp ne %s %s, 0", r.s, ft, v.s);
		return r;
	}
	if (to->kind == TY_INT) {
		if (from->kind == TY_BOOL) {
			emit(G, "%s = zext i1 %s to %s", r.s, v.s, tt);
		} else if (from->kind == TY_INT) {
			if (to->bits == from->bits)
				return v;
			if (to->bits < from->bits)
				emit(G, "%s = trunc %s %s to %s", r.s, ft, v.s, tt);
			else
				emit(G, "%s = %s %s %s to %s", r.s, from->is_signed ? "sext" : "zext", ft, v.s, tt);
		} else {
			const char *name = astr(G, "llvm.fpto%ci.sat.%s.%s", to->is_signed ? 's' : 'u', tt,
						from->bits == 32 ? "f32" : "f64");
			need_intrinsic(G, name, astr(G, "declare %s @%s(%s)", tt, name, ft));
			emit(G, "%s = call %s @%s(%s %s)", r.s, tt, name, ft, v.s);
		}
		return r;
	}
	if (to->kind == TY_FLOAT) {
		if (from->kind == TY_BOOL)
			emit(G, "%s = uitofp i1 %s to %s", r.s, v.s, tt);
		else if (from->kind == TY_INT)
			emit(G, "%s = %s %s %s to %s", r.s, from->is_signed ? "sitofp" : "uitofp", ft, v.s, tt);
		else if (to->bits == from->bits)
			return v;   /* the same size: a kind and its base (meters from double) */
		else if (to->bits > from->bits)
			emit(G, "%s = fpext %s %s to %s", r.s, ft, v.s, tt);
		else
			emit(G, "%s = fptrunc %s %s to %s", r.s, ft, v.s, tt);
		return r;
	}
	return v;
}

static Op to_i64(CG *G, Op v, Type *t)
{
	if (t->kind == TY_INT && t->bits == 64)
		return v;
	Op r = tmp(G);
	if (t->kind == TY_BOOL)
		emit(G, "%s = zext i1 %s to i64", r.s, v.s);
	else
		emit(G, "%s = %s %s %s to i64", r.s, t->is_signed ? "sext" : "zext", llty(G, t), v.s);
	return r;
}

static Op arith(CG *G, BinaryOp op_, Op a, Op b, Type *t, SrcLoc loc)
{
	Op r = tmp(G);
	const char *ty = vty(G, t);
	if (t->kind == TY_FLOAT) {
		const char *ins = op_ == BIN_ADD ? "fadd" : op_ == BIN_SUB ? "fsub" : op_ == BIN_MUL ? "fmul" : "fdiv";
		emit(G, "%s = %s %s %s, %s", r.s, ins, ty, a.s, b.s);
		return r;
	}
	bool sgn = t->kind == TY_INT && t->is_signed;
	int bits = t->kind == TY_BOOL ? 1 : t->bits;
	switch (op_) {
	case BIN_ADD: emit(G, "%s = add %s %s, %s", r.s, ty, a.s, b.s); return r;
	case BIN_SUB: emit(G, "%s = sub %s %s, %s", r.s, ty, a.s, b.s); return r;
	case BIN_MUL: emit(G, "%s = mul %s %s, %s", r.s, ty, a.s, b.s); return r;
	case BIN_BIT_AND: emit(G, "%s = and %s %s, %s", r.s, ty, a.s, b.s); return r;
	case BIN_BIT_OR:  emit(G, "%s = or %s %s, %s", r.s, ty, a.s, b.s); return r;
	case BIN_BIT_XOR: emit(G, "%s = xor %s %s, %s", r.s, ty, a.s, b.s); return r;
	case BIN_DIV:
	case BIN_MOD: {
		bool rem = op_ == BIN_MOD;
		Op z = tmp(G), bad = lbl(G, "divbad"), ok = lbl(G, "divok");
		emit(G, "%s = icmp eq %s %s, 0", z.s, ty, b.s);
		Op cond = z;
		if (sgn && !rem) {
			int64_t min = bits == 64 ? INT64_MIN : -((int64_t)1 << (bits - 1));
			Op m1 = tmp(G), mn = tmp(G), both = tmp(G), any = tmp(G);
			emit(G, "%s = icmp eq %s %s, -1", m1.s, ty, b.s);
			emit(G, "%s = icmp eq %s %s, %" PRId64, mn.s, ty, a.s, min);
			emit(G, "%s = and i1 %s, %s", both.s, m1.s, mn.s);
			emit(G, "%s = or i1 %s, %s", any.s, z.s, both.s);
			cond = any;
		}
		emit_term(G, "br i1 %s, label %%%s, label %%%s, !prof !0", cond.s, bad.s, ok.s);
		place_label(G, bad);
		Op a64 = to_i64(G, a, t), b64 = to_i64(G, b, t), f = file_ref(G, loc);
		emit(G, "call void @rt_check_div(i64 %s, i64 %s, i32 %d, i1 %s, i1 %s, ptr %s, i32 %d, i32 %d)",
		     a64.s, b64.s, bits, sgn ? "true" : "false", rem ? "true" : "false", f.s, loc.line, loc.col);
		emit_term(G, "unreachable");
		place_label(G, ok);
		if (sgn && rem) {
			/* x % -1 is 0, but srem with MIN/-1 is undefined in LLVM: divide by 1 instead */
			Op m1 = tmp(G), d = tmp(G);
			emit(G, "%s = icmp eq %s %s, -1", m1.s, ty, b.s);
			emit(G, "%s = select i1 %s, %s 1, %s %s", d.s, m1.s, ty, ty, b.s);
			emit(G, "%s = srem %s %s, %s", r.s, ty, a.s, d.s);
		} else {
			emit(G, "%s = %s %s %s, %s", r.s, rem ? (sgn ? "srem" : "urem") : (sgn ? "sdiv" : "udiv"), ty, a.s, b.s);
		}
		return r;
	}
	default:
		break;
	}
	return r;
}

/* a << b / a >> b: the amount is range-checked in its own type, then
 * converted to the result type (LLVM shifts need equal widths). */
static Op gen_shift(CG *G, BinaryOp op_, Op a, Op b, Type *t, Type *bt, SrcLoc loc)
{
	int bits = t->bits;
	Op c = tmp(G), bad = lbl(G, "shbad"), ok = lbl(G, "shok");
	emit(G, "%s = icmp uge %s %s, %d", c.s, vty(G, bt), b.s, bits);
	emit_term(G, "br i1 %s, label %%%s, label %%%s, !prof !0", c.s, bad.s, ok.s);
	place_label(G, bad);
	Op b64 = to_i64(G, b, bt), f = file_ref(G, loc);
	emit(G, "call void @rt_check_shift(i64 %s, i32 %d, ptr %s, i32 %d, i32 %d)", b64.s, bits, f.s, loc.line, loc.col);
	emit_term(G, "unreachable");
	place_label(G, ok);
	Op amount = b;
	if (bt->bits != t->bits) {
		amount = tmp(G);
		emit(G, "%s = %s %s %s to %s", amount.s, bt->bits > t->bits ? "trunc" : "zext", vty(G, bt), b.s, vty(G, t));
	}
	Op r = tmp(G);
	emit(G, "%s = %s %s %s, %s", r.s, op_ == BIN_SHL ? "shl" : t->is_signed ? "ashr" : "lshr", vty(G, t), a.s, amount.s);
	return r;
}

static Op gen_compare(CG *G, Expr *e)
{
	BinaryOp o = e->as.binary.op;
	Expr *l = e->as.binary.lhs, *rr = e->as.binary.rhs;
	Type *t = l->ty;
	Op r = tmp(G);
	if (t->kind == TY_STR) {
		Op a = gen_read(G, l);
		Op b = gen_read(G, rr);
		Op eq = tmp(G);
		emit(G, "%s = call i1 @cby_str_eq(ptr %s, ptr %s)", eq.s, a.s, b.s);
		if (o == BIN_EQ)
			return eq;
		emit(G, "%s = xor i1 %s, true", r.s, eq.s);
		return r;
	}
	Op a = gen_val(G, l);
	Op b = gen_val(G, rr);
	const char *ty = vty(G, t);
	if (t->kind == TY_FLOAT) {
		const char *c = o == BIN_EQ ? "oeq" : o == BIN_NE ? "une" : o == BIN_LT ? "olt" : o == BIN_LE ? "ole" :
				o == BIN_GT ? "ogt" : "oge";
		emit(G, "%s = fcmp %s %s %s, %s", r.s, c, ty, a.s, b.s);
		return r;
	}
	bool sgn = t->kind == TY_INT && t->is_signed;
	const char *c = o == BIN_EQ ? "eq" : o == BIN_NE ? "ne" :
			o == BIN_LT ? (sgn ? "slt" : "ult") : o == BIN_LE ? (sgn ? "sle" : "ule") :
			o == BIN_GT ? (sgn ? "sgt" : "ugt") : (sgn ? "sge" : "uge");
	emit(G, "%s = icmp %s %s %s, %s", r.s, c, ty, a.s, b.s);
	return r;
}

static Op gen_logical(CG *G, Expr *e)
{
	bool is_and = e->as.binary.op == BIN_LOG_AND;
	Op a = gen_val(G, e->as.binary.lhs);
	Op rhs = lbl(G, "rhs"), done = lbl(G, "logic");
	/* the block that branches to `done` with a known value */
	Op from = lbl(G, "lhs");
	br(G, from);
	place_label(G, from);
	if (is_and)
		emit_term(G, "br i1 %s, label %%%s, label %%%s", a.s, rhs.s, done.s);
	else
		emit_term(G, "br i1 %s, label %%%s, label %%%s", a.s, done.s, rhs.s);
	place_label(G, rhs);
	Op b = gen_val(G, e->as.binary.rhs);
	Op end = lbl(G, "rhs.end");
	br(G, end);
	place_label(G, end);
	br(G, done);
	place_label(G, done);
	Op r = tmp(G);
	emit(G, "%s = phi i1 [ %s, %%%s ], [ %s, %%%s ]", r.s, is_and ? "false" : "true", from.s, b.s, end.s);
	return r;
}

/* A str or char operand of + / +=, as a pointer to a %str. */
static Op str_operand(CG *G, Expr *a)
{
	if (a->ty->kind == TY_STR)
		return gen_read(G, a);
	Op c = gen_val(G, a);
	Op byte = entry_alloca(G, "i8", "ch"), s = entry_alloca(G, "%str", "chs");
	Op d = tmp(G), l = tmp(G);
	emit(G, "store i8 %s, ptr %s", c.s, byte.s);
	emit(G, "%s = getelementptr %%str, ptr %s, i32 0, i32 0", d.s, s.s);
	emit(G, "store ptr %s, ptr %s", byte.s, d.s);
	emit(G, "%s = getelementptr %%str, ptr %s, i32 0, i32 1", l.s, s.s);
	emit(G, "store i64 %" PRId64 ", ptr %s", (int64_t)1 | STR_STATIC_BIT, l.s);
	return s;
}

/* s.length, s.copy, a + b, s[i]; `sret` as for gen_call */
static Op gen_str_builtin(CG *G, Expr *e, Op *sret)
{
	ExprVec *args = &e->as.call.args;
	switch (e->as.call.builtin) {
	case BUILTIN_STR_LEN: {
		Op p = gen_read(G, args->items[0]), r = tmp(G);
		emit(G, "%s = call i64 @cby_str_len(ptr %s)", r.s, p.s);
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_SLICE_LEN: {
		Op p = gen_read(G, args->items[0]), lp = tmp(G), r = tmp(G);
		emit(G, "%s = getelementptr %%slice, ptr %s, i32 0, i32 1", lp.s, p.s);
		emit(G, "%s = load i64, ptr %s", r.s, lp.s);
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_STR_GET: {
		/* inline: check the index against the length, then load the byte */
		Op p = gen_read(G, args->items[0]);
		Op i = gen_val(G, args->items[1]);
		Op lp = tmp(G), raw = tmp(G), len = tmp(G), c = tmp(G), bad = lbl(G, "soob"), ok = lbl(G, "sinb");
		emit(G, "%s = getelementptr %%str, ptr %s, i32 0, i32 1", lp.s, p.s);
		emit(G, "%s = load i64, ptr %s", raw.s, lp.s);
		emit(G, "%s = and i64 %s, %" PRId64, len.s, raw.s, (int64_t)~STR_STATIC_BIT);
		emit(G, "%s = icmp uge i64 %s, %s", c.s, i.s, len.s);
		emit_term(G, "br i1 %s, label %%%s, label %%%s, !prof !0", c.s, bad.s, ok.s);
		place_label(G, bad);
		Op f = file_ref(G, e->loc);
		emit(G, "call void @rt_str_bounds(i64 %s, i64 %s, ptr %s, i32 %d, i32 %d)", i.s, len.s, f.s,
		     e->loc.line, e->loc.col);
		emit_term(G, "unreachable");
		place_label(G, ok);
		Op dp = tmp(G), data = tmp(G), bp = tmp(G), r = tmp(G);
		emit(G, "%s = getelementptr %%str, ptr %s, i32 0, i32 0", dp.s, p.s);
		emit(G, "%s = load ptr, ptr %s", data.s, dp.s);
		emit(G, "%s = getelementptr i8, ptr %s, i64 %s", bp.s, data.s, i.s);
		emit(G, "%s = load i8, ptr %s", r.s, bp.s);
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_TEXT: {
		Expr *a = args->items[0];
		Type *t = a->ty;
		Op v = gen_val(G, a);
		int what = t->kind == TY_BOOL ? 4 : t->kind == TY_FLOAT ? (t->bits == 32 ? 3 : 2) : t->is_char ? 5 : t->is_signed ? 0 : 1;
		Op iv = op("0"), dv = op("0.0");
		if (t->kind == TY_FLOAT) {
			dv = t->bits == 32 ? convert(G, v, t, &t_double) : v;
		} else if (t->kind == TY_BOOL) {
			iv = tmp(G);
			emit(G, "%s = zext i1 %s to i64", iv.s, v.s);
		} else {
			iv = to_i64(G, v, t);
		}
		Op dst = sret ? *sret : new_temp_slot(G, &t_str, true);
		emit(G, "call void @cby_text(ptr %s, i32 %d, i64 %s, double %s)", dst.s, what, iv.s, dv.s);
		if (!sret)
			temp_initialized(G, dst);
		return dst;
	}
	case BUILTIN_READ_KEY: {
		Op dst = sret ? *sret : new_temp_slot(G, &t_str, true);
		Op f = file_ref(G, e->loc);
		emit(G, "call void @cby_read_key(ptr %s, ptr %s, i32 %d, i32 %d)", dst.s, f.s, e->loc.line, e->loc.col);
		if (!sret)
			temp_initialized(G, dst);
		return dst;
	}
	case BUILTIN_READ_CHAR: {
		Op r = tmp(G), f = file_ref(G, e->loc);
		emit(G, "%s = call i8 @cby_read_char(ptr %s, i32 %d, i32 %d)", r.s, f.s, e->loc.line, e->loc.col);
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_READ_LINE:
	case BUILTIN_INPUT: {
		Op pr = e->as.call.builtin == BUILTIN_INPUT ? str_operand(G, args->items[0]) : op("null");
		Op dst = sret ? *sret : new_temp_slot(G, &t_str, true);
		Op f = file_ref(G, e->loc);
		if (e->as.call.builtin == BUILTIN_INPUT)
			emit(G, "call void @cby_input(ptr %s, ptr %s, ptr %s, i32 %d, i32 %d)", dst.s, pr.s, f.s, e->loc.line,
			     e->loc.col);
		else
			emit(G, "call void @cby_read_line(ptr %s, ptr %s, i32 %d, i32 %d)", dst.s, f.s, e->loc.line, e->loc.col);
		if (!sret)
			temp_initialized(G, dst);
		return dst;
	}
	case BUILTIN_STR_FIND:
	case BUILTIN_STR_COMPARE: {
		Op a = str_operand(G, args->items[0]), b = str_operand(G, args->items[1]), r = tmp(G);
		if (e->as.call.builtin == BUILTIN_STR_FIND)
			emit(G, "%s = call i64 @cby_str_find(ptr %s, ptr %s)", r.s, a.s, b.s);
		else
			emit(G, "%s = call i32 @cby_str_compare(ptr %s, ptr %s)", r.s, a.s, b.s);
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_STR_CONTAINS:
	case BUILTIN_STR_STARTS_WITH:
	case BUILTIN_STR_ENDS_WITH: {
		Op a = str_operand(G, args->items[0]), b = str_operand(G, args->items[1]), r = tmp(G);
		int what = e->as.call.builtin == BUILTIN_STR_CONTAINS ? 0 : e->as.call.builtin == BUILTIN_STR_STARTS_WITH ? 1 : 2;
		emit(G, "%s = call i1 @cby_str_test(ptr %s, ptr %s, i32 %d)", r.s, a.s, b.s, what);
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_STR_TRIM:
	case BUILTIN_STR_UPPER:
	case BUILTIN_STR_LOWER:
	case BUILTIN_STR_REPLACE:
	case BUILTIN_STR_REPEAT:
	case BUILTIN_STR_PAD_LEFT:
	case BUILTIN_STR_PAD_RIGHT:
	case BUILTIN_STR_CENTERED:
	case BUILTIN_STR_SLICE: {
		Op s = gen_read(G, args->items[0]);
		Op dst = sret ? *sret : new_temp_slot(G, &t_str, true);
		switch (e->as.call.builtin) {
		case BUILTIN_STR_TRIM:
			emit(G, "call void @cby_str_trim(ptr %s, ptr %s)", dst.s, s.s);
			break;
		case BUILTIN_STR_UPPER:
		case BUILTIN_STR_LOWER:
			emit(G, "call void @cby_str_case(ptr %s, ptr %s, i1 %s)", dst.s, s.s,
			     e->as.call.builtin == BUILTIN_STR_UPPER ? "true" : "false");
			break;
		case BUILTIN_STR_REPLACE: {
			Op f = str_operand(G, args->items[1]), t = str_operand(G, args->items[2]);
			emit(G, "call void @cby_str_replace(ptr %s, ptr %s, ptr %s, ptr %s)", dst.s, s.s, f.s, t.s);
			break;
		}
		case BUILTIN_STR_REPEAT: {
			Op n = gen_val(G, args->items[1]), f = file_ref(G, e->loc);
			emit(G, "call void @cby_str_repeat(ptr %s, ptr %s, i64 %s, ptr %s, i32 %d, i32 %d)", dst.s, s.s, n.s,
			     f.s, e->loc.line, e->loc.col);
			break;
		}
		case BUILTIN_STR_PAD_LEFT:
		case BUILTIN_STR_PAD_RIGHT:
		case BUILTIN_STR_CENTERED: {
			Op n = gen_val(G, args->items[1]);
			emit(G, "call void @cby_str_pad(ptr %s, ptr %s, i64 %s, i32 %d)", dst.s, s.s, n.s,
			     (int)(e->as.call.builtin - BUILTIN_STR_PAD_LEFT));
			break;
		}
		default: {
			Op a = to_i64(G, gen_val(G, args->items[1]), args->items[1]->ty);
			Op b = to_i64(G, gen_val(G, args->items[2]), args->items[2]->ty);
			Op f = file_ref(G, e->loc);
			emit(G, "call void @cby_str_slice(ptr %s, ptr %s, i64 %s, i64 %s, i32 %d, ptr %s, i32 %d, i32 %d)",
			     dst.s, s.s, a.s, b.s, (int)args->items[3]->as.int_lit.value, f.s, e->loc.line, e->loc.col);
			break;
		}
		}
		if (!sret)
			temp_initialized(G, dst);
		return dst;
	}
	case BUILTIN_FILES_READ: case BUILTIN_FILES_LINES: case BUILTIN_FILES_WRITE: case BUILTIN_FILES_APPEND:
	case BUILTIN_FILES_EXISTS: case BUILTIN_FILES_SIZE: case BUILTIN_FILES_COPY: case BUILTIN_FILES_RENAME:
	case BUILTIN_FILES_DELETE: case BUILTIN_DIRS_LIST: case BUILTIN_DIRS_ALL_FILES: case BUILTIN_DIRS_EXISTS:
	case BUILTIN_DIRS_MAKE: case BUILTIN_DIRS_REMOVE: case BUILTIN_DIRS_RENAME: case BUILTIN_DIRS_CURRENT:
	case BUILTIN_DIRS_CHANGE_TO: case BUILTIN_DIRS_HOME: case BUILTIN_DIRS_SAVE_FOLDER:
	case BUILTIN_FILES_OPEN: case BUILTIN_FILES_OPEN_TO_WRITE:
	case BUILTIN_FILES_OPEN_TO_ADD: case BUILTIN_FILES_OPEN_TO_CHANGE: case BUILTIN_SHELL_RUN: case BUILTIN_SHELL_STATUS:
	case BUILTIN_SHELL_GET: case BUILTIN_SHELL_HAS: case BUILTIN_SHELL_SET: case BUILTIN_SHELL_UNSET: {
		/* files.* and directories.*: the runtime does the work */
		Builtin b = (Builtin)e->as.call.builtin;
		Op a0 = args->len > 0 ? str_operand(G, args->items[0]) : op("null");
		Op a1 = args->len > 1 ? str_operand(G, args->items[1]) : op("null");
		Op f = file_ref(G, e->loc);
		char where[160];
		snprintf(where, sizeof where, "ptr %s, i32 %d, i32 %d", f.s, e->loc.line, e->loc.col);
		Op r = op("undef");
		bool owned = e->ty->kind == TY_STR || e->ty->kind == TY_LIST || e->ty->kind == TY_FILE;
		Op dst = owned ? (sret ? *sret : new_temp_slot(G, e->ty, true)) : op("undef");
		switch (b) {
		case BUILTIN_FILES_READ:
			emit(G, "call void @cby_files_read(ptr %s, ptr %s, %s)", dst.s, a0.s, where);
			break;
		case BUILTIN_FILES_LINES:
			emit(G, "call void @cby_files_lines(ptr %s, ptr %s, %s)", dst.s, a0.s, where);
			break;
		case BUILTIN_DIRS_LIST:
		case BUILTIN_DIRS_ALL_FILES:
			emit(G, "call void @cby_dirs_list(ptr %s, ptr %s, i1 %s, %s)", dst.s, a0.s,
			     b == BUILTIN_DIRS_ALL_FILES ? "true" : "false", where);
			break;
		case BUILTIN_DIRS_CURRENT:
			emit(G, "call void @cby_dirs_current(ptr %s)", dst.s);
			break;
		case BUILTIN_DIRS_HOME:
			emit(G, "call void @cby_dirs_home(ptr %s)", dst.s);
			break;
		case BUILTIN_DIRS_SAVE_FOLDER:
			emit(G, "call void @cby_dirs_save_folder(ptr %s, ptr %s, %s)", dst.s, a0.s, where);
			break;
		case BUILTIN_SHELL_RUN:
		case BUILTIN_SHELL_GET:
			emit(G, "call void @%s(ptr %s, ptr %s, %s)", b == BUILTIN_SHELL_RUN ? "cby_shell_run" : "cby_shell_get",
			     dst.s, a0.s, where);
			break;
		case BUILTIN_SHELL_STATUS:
			r = tmp(G);
			emit(G, "%s = call i32 @cby_shell_status(ptr %s, %s)", r.s, a0.s, where);
			break;
		case BUILTIN_SHELL_HAS:
			r = tmp(G);
			emit(G, "%s = call i1 @cby_shell_has(ptr %s)", r.s, a0.s);
			break;
		case BUILTIN_SHELL_SET:
			emit(G, "call void @cby_shell_set(ptr %s, ptr %s, %s)", a0.s, a1.s, where);
			break;
		case BUILTIN_SHELL_UNSET:
			emit(G, "call void @cby_shell_unset(ptr %s, %s)", a0.s, where);
			break;
		case BUILTIN_FILES_OPEN:
		case BUILTIN_FILES_OPEN_TO_WRITE:
		case BUILTIN_FILES_OPEN_TO_ADD:
		case BUILTIN_FILES_OPEN_TO_CHANGE:
			emit(G, "call void @cby_files_open(ptr %s, ptr %s, i32 %d, %s)", dst.s, a0.s,
			     b == BUILTIN_FILES_OPEN ? RT_OPEN_READ : b == BUILTIN_FILES_OPEN_TO_WRITE ? RT_OPEN_WRITE :
			     b == BUILTIN_FILES_OPEN_TO_ADD ? RT_OPEN_ADD : RT_OPEN_CHANGE,
			     where);
			break;
		case BUILTIN_FILES_WRITE:
		case BUILTIN_FILES_APPEND:
			emit(G, "call void @cby_files_write(ptr %s, ptr %s, i1 %s, %s)", a0.s, a1.s,
			     b == BUILTIN_FILES_APPEND ? "true" : "false", where);
			break;
		case BUILTIN_FILES_EXISTS:
		case BUILTIN_DIRS_EXISTS:
			r = tmp(G);
			emit(G, "%s = call i1 @%s(ptr %s)", r.s, b == BUILTIN_FILES_EXISTS ? "cby_files_exists" : "cby_dirs_exists",
			     a0.s);
			break;
		case BUILTIN_FILES_SIZE:
			r = tmp(G);
			emit(G, "%s = call i64 @cby_files_size(ptr %s, %s)", r.s, a0.s, where);
			break;
		case BUILTIN_FILES_COPY:
		case BUILTIN_FILES_RENAME:
		case BUILTIN_DIRS_RENAME:
			emit(G, "call void @%s(ptr %s, ptr %s, %s)", b == BUILTIN_FILES_COPY ? "cby_files_copy" :
			     b == BUILTIN_FILES_RENAME ? "cby_files_rename" : "cby_dirs_rename", a0.s, a1.s, where);
			break;
		default:
			emit(G, "call void @%s(ptr %s, %s)", b == BUILTIN_FILES_DELETE ? "cby_files_delete" :
			     b == BUILTIN_DIRS_MAKE ? "cby_dirs_make" : b == BUILTIN_DIRS_REMOVE ? "cby_dirs_remove" :
			     "cby_dirs_change_to", a0.s, where);
			break;
		}
		if (owned) {
			if (!sret)
				temp_initialized(G, dst);
			return dst;
		}
		if (sret && e->ty->kind != TY_VOID)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_LOOKUP_LEN: {
		Op r = lk_field(G, gen_read(G, args->items[0]), 5);
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_LOOKUP_CONTAINS: {
		Type *lt = args->items[0]->ty;
		Op l = gen_read(G, args->items[0]);
		Op k = lk_key(G, args->items[1], lt->key), r = tmp(G);
		Op pos = lk_find(G, lt, l, k);
		emit(G, "%s = icmp sge i64 %s, 0", r.s, pos.s);
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_LOOKUP_REMOVE: {
		Type *lt = args->items[0]->ty;
		Op l = gen_read(G, args->items[0]);
		Op k = lk_key(G, args->items[1], lt->key);
		Op pos = lk_must_find(G, lt, l, k, e->loc);
		emit_drop(G, lt->inner, lk_at(G, lt, l, pos, 2));
		emit(G, "call void @cby_lookup_kill(ptr %s, i32 %d, i64 %s, i64 %s)", l.s, key_kind(lt->key),
		     elem_size(G, lt->key), pos.s);
		return op("undef");
	}
	case BUILTIN_LOOKUP_CLEAR: {
		Type *lt = args->items[0]->ty;
		Op l = gen_read(G, args->items[0]);
		emit(G, "call void @\"kclear.%d\"(ptr %s)", lookup_id(G, lt), l.s);
		return op("undef");
	}
	case BUILTIN_LOOKUP_COPY:
	case BUILTIN_LOOKUP_KEYS:
	case BUILTIN_LOOKUP_VALUES: {
		Type *lt = args->items[0]->ty;
		Op l = gen_read(G, args->items[0]);
		Op dst = sret ? *sret : new_temp_slot(G, e->ty, true);
		if (e->as.call.builtin == BUILTIN_LOOKUP_COPY)
			emit(G, "call void @\"kcopy.%d\"(ptr %s, ptr %s)", lookup_id(G, lt), dst.s, l.s);
		else if (e->as.call.builtin == BUILTIN_LOOKUP_VALUES)
			emit(G, "call void @\"kvalues.%d\"(ptr %s, ptr %s)", lookup_id(G, lt), dst.s, l.s);
		else
			emit(G, "call void @cby_lookup_keys(ptr %s, ptr %s, i32 %d, i64 %s)", dst.s, l.s, key_kind(lt->key),
			     elem_size(G, lt->key));
		if (!sret)
			temp_initialized(G, dst);
		return dst;
	}
	case BUILTIN_LOOKUP_NEXT: {
		Op l = gen_read(G, args->items[0]);
		Op from = gen_val(G, args->items[1]), r = tmp(G);
		emit(G, "%s = call i64 @cby_lookup_next(ptr %s, i64 %s)", r.s, l.s, from.s);
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_LIST_LEN: {
		Op l = gen_read(G, args->items[0]), lp = tmp(G), r = tmp(G);
		emit(G, "%s = getelementptr %%list, ptr %s, i32 0, i32 1", lp.s, l.s);
		emit(G, "%s = load i64, ptr %s", r.s, lp.s);
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_LIST_ADD:
	case BUILTIN_LIST_INSERT: {
		Type *el = args->items[0]->ty->inner;
		Op l = gen_read(G, args->items[0]);
		Op at = e->as.call.builtin == BUILTIN_LIST_INSERT ? gen_val(G, args->items[1]) : op("0");
		/* the value first, then the room for it (finding room may move the elements) */
		Op v = new_temp_slot(G, el, false);
		gen_into(G, args->items[args->len - 1], v);
		Op slot = tmp(G);
		if (e->as.call.builtin == BUILTIN_LIST_ADD) {
			/* inline when there's room; the runtime only when the list must grow */
			Op lp = tmp(G), cp = tmp(G), dp = tmp(G), n = tmp(G), c = tmp(G), room = tmp(G);
			Op fast = lbl(G, "add.room"), slow = lbl(G, "add.grow"), done = lbl(G, "add.done");
			Op d = tmp(G), fs = tmp(G), n1 = tmp(G), ss = tmp(G);
			emit(G, "%s = getelementptr %%list, ptr %s, i32 0, i32 1", lp.s, l.s);
			emit(G, "%s = getelementptr %%list, ptr %s, i32 0, i32 2", cp.s, l.s);
			emit(G, "%s = load i64, ptr %s", n.s, lp.s);
			emit(G, "%s = load i64, ptr %s", c.s, cp.s);
			emit(G, "%s = icmp slt i64 %s, %s", room.s, n.s, c.s);
			emit_term(G, "br i1 %s, label %%%s, label %%%s", room.s, fast.s, slow.s);
			place_label(G, fast);
			emit(G, "%s = getelementptr %%list, ptr %s, i32 0, i32 0", dp.s, l.s);
			emit(G, "%s = load ptr, ptr %s", d.s, dp.s);
			emit(G, "%s = getelementptr %s, ptr %s, i64 %s", fs.s, llty(G, el), d.s, n.s);
			emit(G, "%s = add i64 %s, 1", n1.s, n.s);
			emit(G, "store i64 %s, ptr %s", n1.s, lp.s);
			br(G, done);
			place_label(G, slow);
			emit(G, "%s = call ptr @cby_list_push(ptr %s, i64 %s)", ss.s, l.s, elem_size(G, el));
			br(G, done);
			place_label(G, done);
			emit(G, "%s = phi ptr [ %s, %%%s ], [ %s, %%%s ]", slot.s, fs.s, fast.s, ss.s, slow.s);
		} else {
			Op f = file_ref(G, e->loc);
			emit(G, "%s = call ptr @cby_list_insert_slot(ptr %s, i64 %s, i64 %s, ptr %s, i32 %d, i32 %d)", slot.s,
			     l.s, elem_size(G, el), at.s, f.s, e->loc.line, e->loc.col);
		}
		memcpy_(G, slot, v, size_of(el));
		return op("undef");
	}
	case BUILTIN_LIST_REMOVE_AT: {
		Type *el = args->items[0]->ty->inner;
		Op l = gen_read(G, args->items[0]);
		Op at = gen_val(G, args->items[1]), f = file_ref(G, e->loc), p = tmp(G);
		emit(G, "%s = call ptr @cby_list_at(ptr %s, i64 %s, i64 %s, ptr %s, i32 %d, i32 %d)", p.s, l.s,
		     elem_size(G, el), at.s, f.s, e->loc.line, e->loc.col);
		emit_drop(G, el, p);
		emit(G, "call void @cby_list_close_gap(ptr %s, i64 %s, i64 %s)", l.s, elem_size(G, el), at.s);
		return op("undef");
	}
	case BUILTIN_LIST_TAKE_LAST: {
		Type *el = e->ty;
		Op l = gen_read(G, args->items[0]), f = file_ref(G, e->loc), p = tmp(G);
		emit(G, "%s = call ptr @cby_list_take_last(ptr %s, i64 %s, ptr %s, i32 %d, i32 %d)", p.s, l.s,
		     elem_size(G, el), f.s, e->loc.line, e->loc.col);
		if (sret) {
			memcpy_(G, *sret, p, size_of(el));
			return *sret;
		}
		if (is_agg(el)) {
			Op dst = new_temp_slot(G, el, true);
			memcpy_(G, dst, p, size_of(el));
			temp_initialized(G, dst);
			return dst;
		}
		return load(G, el, p, false);
	}
	case BUILTIN_LIST_SORT: {
		Type *el = args->items[0]->ty->inner;
		Op l = gen_read(G, args->items[0]);
		int kind;
		if (el->kind == TY_STR)
			kind = RT_SORT_STR;
		else if (el->kind == TY_FLOAT)
			kind = el->bits == 32 ? RT_SORT_F32 : RT_SORT_F64;
		else if (el->kind == TY_BOOL)
			kind = RT_SORT_U8;
		else {
			int sz = el->bits == 8 ? 0 : el->bits == 16 ? 1 : el->bits == 32 ? 2 : 3;
			kind = (el->is_signed && !el->is_char ? RT_SORT_I8 : RT_SORT_U8) + sz;
		}
		emit(G, "call void @cby_list_sort(ptr %s, i32 %d)", l.s, kind);
		return op("undef");
	}
	case BUILTIN_LIST_SORT_BY:
	case BUILTIN_LIST_SORT_WITH: {
		Type *el = args->items[0]->ty->inner;
		Op l = gen_read(G, args->items[0]);
		int id = G->sorts.len;
		vec_push(&G->A, G->sorts, e);
		emit(G, "call void @cby_list_sort_by(ptr %s, i64 %s, ptr @\"sbefore.%d\")", l.s, elem_size(G, el), id);
		return op("undef");
	}
	case BUILTIN_LIST_REVERSE: {
		Type *el = args->items[0]->ty->inner;
		Op l = gen_read(G, args->items[0]);
		emit(G, "call void @cby_list_reverse(ptr %s, i64 %s)", l.s, elem_size(G, el));
		return op("undef");
	}
	case BUILTIN_LIST_CLEAR: {
		Type *el = args->items[0]->ty->inner;
		Op l = gen_read(G, args->items[0]);
		emit(G, "call void @\"lclear.%d\"(ptr %s)", list_id(G, el), l.s);
		return op("undef");
	}
	case BUILTIN_LIST_CONTAINS:
	case BUILTIN_LIST_FIND: {
		Type *el = args->items[0]->ty->inner;
		Op l = gen_read(G, args->items[0]);
		Op x;
		if (el->kind == TY_STR) {
			x = str_operand(G, args->items[1]);
		} else {
			Op v = gen_val(G, args->items[1]);
			x = entry_alloca(G, llty(G, el), "want");
			store(G, el, v, x, false);
		}
		Op r = tmp(G);
		emit(G, "%s = call i64 @\"lfind.%d\"(ptr %s, ptr %s)", r.s, list_id(G, el), l.s, x.s);
		if (e->as.call.builtin == BUILTIN_LIST_CONTAINS) {
			Op b = tmp(G);
			emit(G, "%s = icmp sge i64 %s, 0", b.s, r.s);
			r = b;
		}
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_LIST_COPY:
	case BUILTIN_LIST_SLICE: {
		Type *el = args->items[0]->ty->inner;
		Op l = gen_read(G, args->items[0]);
		Op dst = sret ? *sret : new_temp_slot(G, e->ty, true);
		if (e->as.call.builtin == BUILTIN_LIST_COPY) {
			emit(G, "call void @\"lcopy.%d\"(ptr %s, ptr %s, i64 0, i64 -1)", list_id(G, el), dst.s, l.s);
		} else {
			Op a = to_i64(G, gen_val(G, args->items[1]), args->items[1]->ty);
			Op b = to_i64(G, gen_val(G, args->items[2]), args->items[2]->ty);
			Op lp = tmp(G), n = tmp(G), sp = entry_alloca(G, "i64", "from"), cp = entry_alloca(G, "i64", "count");
			Op f = file_ref(G, e->loc), st = tmp(G), ct = tmp(G);
			emit(G, "%s = getelementptr %%list, ptr %s, i32 0, i32 1", lp.s, l.s);
			emit(G, "%s = load i64, ptr %s", n.s, lp.s);
			emit(G, "call void @rt_list_slice(i64 %s, i64 %s, i64 %s, i32 %d, ptr %s, ptr %s, ptr %s, i32 %d, i32 %d)",
			     n.s, a.s, b.s, (int)args->items[3]->as.int_lit.value, sp.s, cp.s, f.s, e->loc.line, e->loc.col);
			emit(G, "%s = load i64, ptr %s", st.s, sp.s);
			emit(G, "%s = load i64, ptr %s", ct.s, cp.s);
			emit(G, "call void @\"lcopy.%d\"(ptr %s, ptr %s, i64 %s, i64 %s)", list_id(G, el), dst.s, l.s, st.s, ct.s);
		}
		if (!sret)
			temp_initialized(G, dst);
		return dst;
	}
	case BUILTIN_LIST_JOIN:
	case BUILTIN_STR_SPLIT: {
		Op a = e->as.call.builtin == BUILTIN_LIST_JOIN ? gen_read(G, args->items[0]) : str_operand(G, args->items[0]);
		Op b = str_operand(G, args->items[1]);
		Op dst = sret ? *sret : new_temp_slot(G, e->ty, true);
		if (e->as.call.builtin == BUILTIN_LIST_JOIN)
			emit(G, "call void @cby_list_join(ptr %s, ptr %s, ptr %s)", dst.s, a.s, b.s);
		else
			emit(G, "call void @cby_str_split(ptr %s, ptr %s, ptr %s)", dst.s, a.s, b.s);
		if (!sret)
			temp_initialized(G, dst);
		return dst;
	}
	case BUILTIN_TO_FILE: {
		Op fp = gen_read(G, args->items[0]), f = file_ref(G, e->loc);
		emit(G, "call void @cby_out_push_file(ptr %s, ptr %s, i32 %d, i32 %d)", fp.s, f.s, e->loc.line, e->loc.col);
		Op r = gen_call(G, args->items[1], sret);
		emit(G, "call void @rt_out_pop()");
		return r;
	}
	case BUILTIN_FILE_HANDLE: {
		/* the rt_file the slot holds, as a number for the library's C */
		Op fp = gen_read(G, args->items[0]);
		Op p = tmp(G), r = tmp(G);
		emit(G, "%s = load ptr, ptr %s", p.s, fp.s);
		emit(G, "%s = ptrtoint ptr %s to i64", r.s, p.s);
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_FILE_READ_LINE:
	case BUILTIN_FILE_AT_END: {
		Op fp = gen_read(G, args->items[0]), f = file_ref(G, e->loc);
		if (e->as.call.builtin == BUILTIN_FILE_AT_END) {
			Op r = tmp(G);
			emit(G, "%s = call i1 @cby_file_at_end(ptr %s, ptr %s, i32 %d, i32 %d)", r.s, fp.s, f.s, e->loc.line,
			     e->loc.col);
			if (sret)
				store(G, e->ty, r, *sret, false);
			return r;
		}
		Op dst = sret ? *sret : new_temp_slot(G, e->ty, true);
		emit(G, "call void @cby_file_read_line(ptr %s, ptr %s, ptr %s, i32 %d, i32 %d)", dst.s, fp.s, f.s, e->loc.line,
		     e->loc.col);
		if (!sret)
			temp_initialized(G, dst);
		return dst;
	}
	case BUILTIN_HANDLER_SET: {
		/* { the object, its method }: the object's block never moves */
		Op dst = gen_place(G, args->items[0]);
		Op v = gen_val(G, args->items[1]);
		emit(G, "store { ptr, ptr } %s, ptr %s", v.s, dst.s);
		return op("undef");
	}
	case BUILTIN_HANDLER_CLEAR: {
		Op dst = gen_place(G, args->items[0]);
		emit(G, "store { ptr, ptr } zeroinitializer, ptr %s", dst.s);
		return op("undef");
	}
	case BUILTIN_ADDRESSES_ALLOCATE:
	case BUILTIN_ADDRESSES_FREE:
	case BUILTIN_ADDRESSES_REALLOCATE: {
		Builtin b = (Builtin)e->as.call.builtin;
		Op f = file_ref(G, e->loc), r = op("undef");
		if (b == BUILTIN_ADDRESSES_FREE) {
			emit(G, "call void @free(ptr %s)", gen_val(G, args->items[0]).s);
			return r;
		}
		uint64_t es = size_of(e->ty->inner);
		r = tmp(G);
		if (b == BUILTIN_ADDRESSES_ALLOCATE)
			emit(G, "%s = call ptr @cby_allocate(i64 %s, i64 %" PRIu64 ", ptr %s, i32 %d, i32 %d)", r.s,
			     gen_val(G, args->items[0]).s, es, f.s, e->loc.line, e->loc.col);
		else {
			Op a = gen_val(G, args->items[0]);
			emit(G, "%s = call ptr @cby_reallocate(ptr %s, i64 %s, i64 %" PRIu64 ", ptr %s, i32 %d, i32 %d)", r.s, a.s,
			     gen_val(G, args->items[1]).s, es, f.s, e->loc.line, e->loc.col);
		}
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_EVENTS_RUN:
	case BUILTIN_EVENTS_STOP:
		emit(G, "call void @%s()", e->as.call.builtin == BUILTIN_EVENTS_RUN ? "cby_events_run" : "cby_events_stop");
		return op("undef");
	case BUILTIN_ADDRESSES_OF_TEXT:
	case BUILTIN_ADDRESSES_OF_ITEMS: {
		Op p = gen_place(G, args->items[0]), r = tmp(G);
		if (e->as.call.builtin == BUILTIN_ADDRESSES_OF_TEXT) {
			emit(G, "%s = call ptr @cby_text_address(ptr %s)", r.s, p.s);
		} else {
			Op dp = tmp(G);
			emit(G, "%s = getelementptr %%list, ptr %s, i32 0, i32 0", dp.s, p.s);
			emit(G, "%s = load ptr, ptr %s", r.s, dp.s);
		}
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_LINK_IS_SET: {
		Op l = gen_val(G, args->items[0]), r = tmp(G);
		emit(G, "%s = call i1 @cby_link_is_set(i64 %s)", r.s, l.s);
		return r;
	}
	case BUILTIN_LINK_CLEAR: {
		Op p = gen_place(G, args->items[0]);
		emit(G, "store i64 0, ptr %s", p.s);
		return op("undef");
	}
	case BUILTIN_ADDRESSES_OF:
	case BUILTIN_ADDRESSES_AT:
	case BUILTIN_ADDRESSES_NONE:
	case BUILTIN_ADDRESS_NUMBER:
	case BUILTIN_ADDRESS_IS_NONE:
	case BUILTIN_ADDRESS_DIFF: {
		Builtin b = (Builtin)e->as.call.builtin;
		Op r = tmp(G);
		if (b == BUILTIN_ADDRESSES_OF) {
			r = gen_place(G, args->items[0]);
		} else if (b == BUILTIN_ADDRESSES_AT) {
			emit(G, "%s = inttoptr i64 %s to ptr", r.s, gen_val(G, args->items[0]).s);
		} else if (b == BUILTIN_ADDRESSES_NONE) {
			r = op("null");
		} else if (b == BUILTIN_ADDRESS_NUMBER) {
			emit(G, "%s = ptrtoint ptr %s to i64", r.s, gen_val(G, args->items[0]).s);
		} else if (b == BUILTIN_ADDRESS_IS_NONE) {
			emit(G, "%s = icmp eq ptr %s, null", r.s, gen_val(G, args->items[0]).s);
		} else {
			Op a = gen_val(G, args->items[0]), c = gen_val(G, args->items[1]), x = tmp(G), y = tmp(G), d = tmp(G);
			emit(G, "%s = ptrtoint ptr %s to i64", x.s, a.s);
			emit(G, "%s = ptrtoint ptr %s to i64", y.s, c.s);
			emit(G, "%s = sub i64 %s, %s", d.s, x.s, y.s);
			emit(G, "%s = sdiv exact i64 %s, %" PRIu64, r.s, d.s, size_of(args->items[0]->ty->inner));
		}
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_HANDLER_CALL:
	case BUILTIN_HANDLER_IS_SET: {
		Expr *h = args->items[0];
		Op v;
		if (h->kind == EXPR_FIELD) {
			Op p = gen_place(G, h);
			v = tmp(G);
			emit(G, "%s = load { ptr, ptr }, ptr %s", v.s, p.s);
		} else {
			v = gen_val(G, h);
		}
		Op obj = tmp(G), fn = tmp(G), set = tmp(G);
		emit(G, "%s = extractvalue { ptr, ptr } %s, 0", obj.s, v.s);
		emit(G, "%s = extractvalue { ptr, ptr } %s, 1", fn.s, v.s);
		emit(G, "%s = icmp ne ptr %s, null", set.s, fn.s);
		if (e->as.call.builtin == BUILTIN_HANDLER_IS_SET) {
			if (sret)
				store(G, e->ty, set, *sret, false);
			return set;
		}
		Op yes = lbl(G, "h.call"), done = lbl(G, "h.done");
		emit_term(G, "br i1 %s, label %%%s, label %%%s", set.s, yes.s, done.s);
		place_label(G, yes);
		emit(G, "call void %s(ptr %s)", fn.s, obj.s);
		br(G, done);
		place_label(G, done);
		return op("undef");
	}
	case BUILTIN_TO_STDOUT:
	case BUILTIN_TO_STDERR: {
		emit(G, "call void @rt_out_push(i32 %d)", e->as.call.builtin == BUILTIN_TO_STDOUT ? 1 : 2);
		Op r = gen_call(G, args->items[0], sret);
		emit(G, "call void @rt_out_pop()");
		return r;
	}
	case BUILTIN_ABS: {
		Type *t = e->ty;
		Op v = gen_val(G, args->items[0]), r = tmp(G);
		if (t->kind == TY_FLOAT) {
			const char *ft = t->bits == 32 ? "float" : "double";
			need_intrinsic(G, t->bits == 32 ? "llvm.fabs.f32" : "llvm.fabs.f64",
				       t->bits == 32 ? "declare float @llvm.fabs.f32(float)" : "declare double @llvm.fabs.f64(double)");
			emit(G, "%s = call %s @llvm.fabs.%s(%s %s)", r.s, ft, t->bits == 32 ? "f32" : "f64", ft, v.s);
		} else if (!t->is_signed) {
			r = v;
		} else {
			Op neg = tmp(G), lt = tmp(G);
			const char *it = vty(G, t);
			emit(G, "%s = sub %s 0, %s", neg.s, it, v.s);
			emit(G, "%s = icmp slt %s %s, 0", lt.s, it, v.s);
			emit(G, "%s = select i1 %s, %s %s, %s %s", r.s, lt.s, it, neg.s, it, v.s);
		}
		if (sret)
			store(G, t, r, *sret, false);
		return r;
	}
	case BUILTIN_CLOSE_TO: {
		Op a = gen_val(G, args->items[0]), b = gen_val(G, args->items[1]);
		const char *limit = "0.0";
		Op l;
		if (args->len > 2) {
			l = gen_val(G, args->items[2]);
			limit = l.s;
		}
		Op c = tmp(G), r = tmp(G);
		emit(G, "%s = call i32 @rt_close_to(double %s, double %s, double %s, i32 %d)", c.s, a.s, b.s, limit,
		     (e->as.call.close_floats ? 1 : 0) | (args->len > 2 ? 2 : 0));
		emit(G, "%s = icmp ne i32 %s, 0", r.s, c.s);
		if (sret)
			store(G, &t_bool, r, *sret, false);
		return r;
	}
	case BUILTIN_RANDOM: {
		Type *t = e->ty;
		Op a = gen_val(G, args->items[0]), b = gen_val(G, args->items[1]), r = tmp(G);
		if (t->kind == TY_FLOAT) {
			emit(G, "%s = call double @rt_random_double(double %s, double %s)", r.s, a.s, b.s);
		} else {
			Op r64 = tmp(G);
			emit(G, "%s = call i64 @rt_random_int(i64 %s, i64 %s)", r64.s, to_i64(G, a, t).s, to_i64(G, b, t).s);
			if (t->bits == 64)
				r = r64;
			else
				emit(G, "%s = trunc i64 %s to %s", r.s, r64.s, vty(G, t));
		}
		if (sret)
			store(G, t, r, *sret, false);
		return r;
	}
	case BUILTIN_RANDOM_SEED: {
		Op v = gen_val(G, args->items[0]);
		emit(G, "call void @rt_random_seed(i64 %s)", v.s);
		return op("undef");
	}
	case BUILTIN_EXIT: {
		Op v = args->len ? gen_val(G, args->items[0]) : op("0");
		emit(G, "call void @cby_exit(i32 %s)", v.s);
		return op("undef");
	}
	case BUILTIN_TEXT_DECIMALS: {
		Expr *a = args->items[0];
		Op v = gen_val(G, a), p = gen_val(G, args->items[1]);
		Op dst = sret ? *sret : new_temp_slot(G, &t_str, true);
		Op f = file_ref(G, e->loc);
		emit(G, "call void @rt_set_lib_call(ptr %s, i32 %d, i32 %d)", f.s, e->loc.line, e->loc.col);
		if (a->ty->kind == TY_FLOAT)
			emit(G, "call void @cby_text_decimals(ptr %s, double %s, i64 %s)", dst.s, v.s, p.s);
		else
			emit(G, "call void @cby_text_decimals_int(ptr %s, i64 %s, i1 %s, i64 %s)", dst.s, to_i64(G, v, a->ty).s,
			     a->ty->is_signed ? "true" : "false", p.s);
		emit(G, "call void @rt_set_lib_call(ptr null, i32 0, i32 0)");
		if (!sret)
			temp_initialized(G, dst);
		return dst;
	}
	case BUILTIN_RECOVERED: {
		Op dst = sret ? *sret : new_temp_slot(G, &t_str, true);
		emit(G, "call void @cby_recovered(ptr %s)", dst.s);
		if (!sret)
			temp_initialized(G, dst);
		return dst;
	}
	case BUILTIN_END_OF_INPUT: {
		Op r = tmp(G);
		emit(G, "%s = call i1 @cby_end_of_input()", r.s);
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_STR_IS_INT:
	case BUILTIN_STR_IS_LONG:
	case BUILTIN_STR_IS_NUMBER: {
		Op a = str_operand(G, args->items[0]), r = tmp(G);
		int what = e->as.call.builtin == BUILTIN_STR_IS_INT ? 0 : e->as.call.builtin == BUILTIN_STR_IS_LONG ? 1 : 2;
		emit(G, "%s = call i1 @cby_str_is_number(ptr %s, i32 %d)", r.s, a.s, what);
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_STR_TO_INT:
	case BUILTIN_STR_TO_LONG: {
		Op a = str_operand(G, args->items[0]), f = file_ref(G, e->loc), r = tmp(G);
		bool is_int = e->as.call.builtin == BUILTIN_STR_TO_INT;
		emit(G, "%s = call i64 @cby_str_to_int(ptr %s, i32 %d, ptr %s, i32 %d, i32 %d)", r.s, a.s,
		     is_int ? 0 : 1, f.s, e->loc.line, e->loc.col);
		if (is_int) {
			Op t = tmp(G);
			emit(G, "%s = trunc i64 %s to i32", t.s, r.s);
			r = t;
		}
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_STR_TO_DOUBLE: {
		Op a = str_operand(G, args->items[0]), f = file_ref(G, e->loc), r = tmp(G);
		emit(G, "%s = call double @cby_str_to_double(ptr %s, ptr %s, i32 %d, i32 %d)", r.s, a.s, f.s,
		     e->loc.line, e->loc.col);
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}
	case BUILTIN_STR_CONCAT: {
		/* a chain a + b + c ...: join all the parts at once */
		Expr *parts[64];
		int n = 0;
		Expr *stack[64];
		int sp = 0;
		stack[sp++] = e;
		while (sp > 0 && n < 64) {
			Expr *x = stack[--sp];
			if (x->kind == EXPR_CALL && x->as.call.builtin == BUILTIN_STR_CONCAT && sp + 2 <= 64) {
				stack[sp++] = x->as.call.args.items[1];   /* right later */
				stack[sp++] = x->as.call.args.items[0];   /* left first */
			} else {
				parts[n++] = x;
			}
		}
		if (n > 2 && sp == 0) {
			Op ops[64];
			for (int i = 0; i < n; i++)
				ops[i] = str_operand(G, parts[i]);
			Op arr = entry_alloca(G, astr(G, "[%d x ptr]", n), "parts");
			for (int i = 0; i < n; i++) {
				Op slot = tmp(G);
				emit(G, "%s = getelementptr [%d x ptr], ptr %s, i64 0, i64 %d", slot.s, n, arr.s, i);
				emit(G, "store ptr %s, ptr %s", ops[i].s, slot.s);
			}
			Op dst = sret ? *sret : new_temp_slot(G, &t_str, true);
			emit(G, "call void @cby_str_concat_n(ptr %s, ptr %s, i32 %d)", dst.s, arr.s, n);
			if (!sret)
				temp_initialized(G, dst);
			return dst;
		}
	}
	/* fall through */
	case BUILTIN_STR_COPY: {
		Op a = str_operand(G, args->items[0]);
		Op b = e->as.call.builtin == BUILTIN_STR_CONCAT ? str_operand(G, args->items[1]) : op("null");
		Op dst = sret ? *sret : new_temp_slot(G, &t_str, true);
		if (e->as.call.builtin == BUILTIN_STR_COPY)
			emit(G, "call void @cby_str_copy(ptr %s, ptr %s)", dst.s, a.s);
		else
			emit(G, "call void @cby_str_concat(ptr %s, ptr %s, ptr %s)", dst.s, a.s, b.s);
		if (!sret)
			temp_initialized(G, dst);
		return dst;
	}
	default:
		return op("undef");
	}
}

static void gen_assign(CG *G, Expr *e)
{
	Expr *target = e->as.assign.target;
	Type *tt = target->ty;
	if (e->as.assign.compound && tt->kind == TY_STR) {
		Op v = str_operand(G, e->as.assign.value);
		Op p = gen_place(G, target);
		emit(G, "call void @cby_str_append(ptr %s, ptr %s)", p.s, v.s);
		return;
	}
	if (target->kind == EXPR_INDEX && target->as.index.on_str) {
		Op v = gen_val(G, e->as.assign.value);
		Op p = gen_place(G, target->as.index.base);
		Op i = gen_val(G, target->as.index.index);
		Op f = file_ref(G, target->loc);
		if (e->as.assign.compound) {
			Op old = tmp(G);
			emit(G, "%s = call i8 @cby_str_get(ptr %s, i64 %s, ptr %s, i32 %d, i32 %d)", old.s, p.s, i.s, f.s,
			     target->loc.line, target->loc.col);
			Type *ot = e->ty;
			Op oc = convert(G, old, &t_char, ot);
			Op res = arith(G, e->as.assign.op, oc, v, ot, e->loc);
			v = convert(G, res, ot, &t_char);
		}
		emit(G, "call void @cby_str_set(ptr %s, i64 %s, i8 %s, ptr %s, i32 %d, i32 %d)", p.s, i.s, v.s, f.s,
		     target->loc.line, target->loc.col);
		return;
	}
	if (e->as.assign.compound) {
		Op v = gen_val(G, e->as.assign.value);
		Op p = gen_place(G, target);
		bool vol = sym_volatile(target);
		Op old = load(G, tt, p, vol);
		Type *ot = e->ty;
		Op oc = convert(G, old, tt, ot);
		BinaryOp bop = e->as.assign.op;
		Op res = (bop == BIN_SHL || bop == BIN_SHR)
			 ? gen_shift(G, bop, oc, v, ot, e->as.assign.value->ty, e->loc)
			 : arith(G, bop, oc, v, ot, e->loc);
		store(G, tt, convert(G, res, ot, tt), p, vol);
		return;
	}
	if (target->kind == EXPR_INDEX && target->as.index.lookup_put) {
		/* right-hand side first, then the key's place */
		Op src = entry_alloca(G, llty(G, tt), "rhs");
		gen_into(G, e->as.assign.value, src);
		Expr *base = target->as.index.base;
		lk_store(G, base->ty, gen_read(G, base), target->as.index.index, src);
		return;
	}
	/* right-hand side first, then the place */
	bool agg = is_agg(tt);
	Op v, src;
	if (agg) {
		src = entry_alloca(G, llty(G, tt), "rhs");
		gen_into(G, e->as.assign.value, src);
	} else {
		v = gen_val(G, e->as.assign.value);
	}
	Op p = gen_place(G, target);
	Symbol *s = target->kind == EXPR_IDENT ? target->as.ident.sym : NULL;
	bool own_var = s && s->kind != SYM_GLOBAL && s->type->kind != TY_REF;
	if (type_needs_drop(tt)) {
		if (own_var)
			drop_symbol(G, s);      /* only if it currently holds a value */
		else
			emit_drop(G, tt, p);    /* fields, elements and borrowed places always do */
	}
	if (agg)
		memcpy_(G, p, src, size_of(tt));
	else
		store(G, tt, v, p, sym_volatile(target));
	if (own_var && G->flag[s->index])
		set_flag(G, G->flag[s->index], 1);
}

static Op gen_val(CG *G, Expr *e)
{
	Type *t = e->ty;
	switch (e->kind) {
	case EXPR_LINK_TO: {
		Op p = gen_place(G, e->as.link.operand), h = tmp(G), r = tmp(G);
		emit(G, "%s = load ptr, ptr %s", h.s, p.s);
		emit(G, "%s = call i64 @cby_link_obj(ptr %s)", r.s, h.s);
		return r;
	}
	case EXPR_LINK_GET: {
		Op p = gen_place(G, e), h = tmp(G);
		emit(G, "%s = load ptr, ptr %s", h.s, p.s);
		return h;
	}
	case EXPR_INT:
		return iconst(t, (int64_t)e->as.int_lit.value);
	case EXPR_FLOAT:
		return fconst(e->as.float_lit.value, t->bits);
	case EXPR_BOOL:
		return op("%s", e->as.bool_val ? "true" : "false");
	case EXPR_CHAR:
		return op("%d", (int8_t)e->as.char_val);

	case EXPR_IDENT:
	case EXPR_SELF: {
		Symbol *s = e->as.ident.sym;
		Op p = gen_place(G, e);
		Op v = load(G, t, p, s->is_volatile);
		if (e->use == USE_MOVE && s->kind != SYM_GLOBAL && s->type->kind != TY_REF && G->flag[s->index])
			set_flag(G, G->flag[s->index], 0);   /* moved out (object handles) */
		return v;
	}
	case EXPR_FIELD:
	case EXPR_INDEX:
		return load(G, t, gen_place(G, e), false);
	case EXPR_AT:
		return load(G, t, gen_place(G, e), true);   /* memory others may change: read every time */

	case EXPR_UNARY: {
		Expr *opd = e->as.unary.operand;
		if (e->as.unary.op == UN_NEG && opd->kind == EXPR_INT)
			return iconst(t, (int64_t)(0 - opd->as.int_lit.value));
		Op v = gen_val(G, opd);
		Op r = tmp(G);
		switch (e->as.unary.op) {
		case UN_NEG:
			if (t->kind == TY_FLOAT)
				emit(G, "%s = fneg %s %s", r.s, vty(G, t), v.s);
			else
				emit(G, "%s = sub %s 0, %s", r.s, vty(G, t), v.s);
			break;
		case UN_NOT:
			emit(G, "%s = xor i1 %s, true", r.s, v.s);
			break;
		case UN_BITNOT:
			emit(G, "%s = xor %s %s, -1", r.s, vty(G, t), v.s);
			break;
		}
		return r;
	}

	case EXPR_BINARY: {
		BinaryOp o = e->as.binary.op;
		if (ast_binop_is_logical(o))
			return gen_logical(G, e);
		if (ast_binop_is_comparison(o))
			return gen_compare(G, e);
		Op a = gen_val(G, e->as.binary.lhs);
		Op b = gen_val(G, e->as.binary.rhs);
		if (t->kind == TY_ADDRESS) {
			/* A + n, A - n: n whole values along */
			Op n = b, r = tmp(G);
			if (o == BIN_SUB) {
				n = tmp(G);
				emit(G, "%s = sub i64 0, %s", n.s, b.s);
			}
			emit(G, "%s = getelementptr %s, ptr %s, i64 %s", r.s, llty(G, t->inner), a.s, n.s);
			return r;
		}
		if (o == BIN_SHL || o == BIN_SHR)
			return gen_shift(G, o, a, b, t, e->as.binary.rhs->ty, e->loc);
		return arith(G, o, a, b, t, e->loc);
	}

	case EXPR_ASSIGN:
		gen_assign(G, e);
		return op("undef");

	case EXPR_CALL:
		return gen_call(G, e, NULL);

	case EXPR_CAST: {
		Op v = gen_val(G, e->as.cast.value);
		return convert(G, v, e->as.cast.value->ty, t);
	}

	case EXPR_BORROW: {
		Expr *o = e->as.borrow.operand;
		return is_place(o) ? gen_place(G, o) : gen_read(G, o);
	}

	case EXPR_CALLBACK: {
		/* the function itself: C calls it as it calls its own (a function
		 * of numbers and addresses has C's calling convention) */
		FnDecl *f = e->as.callback.fn;
		if (f->is_extern)
			declare_extern(G, f);
		return op("%s", fn_name(G, f));
	}
	case EXPR_HANDLER: {
		/* { the window's object, its (most derived) method } */
		Op h = tmp(G);
		emit(G, "%s = load ptr, ptr %s", h.s, symbol_storage(G, e->as.handler.self).s);
		Op f;
		if (e->as.handler.slot >= 0) {
			Op ci = tmp(G), vt = tmp(G), fp = tmp(G);
			f = tmp(G);
			emit(G, "%s = load ptr, ptr %s", ci.s, h.s);
			emit(G, "%s = load ptr, ptr %s", vt.s, ci.s);
			emit(G, "%s = getelementptr ptr, ptr %s, i64 %d", fp.s, vt.s, e->as.handler.slot);
			emit(G, "%s = load ptr, ptr %s", f.s, fp.s);
		} else {
			f = op("%s", fn_name(G, e->as.handler.method));
		}
		Op a = tmp(G), b = tmp(G);
		emit(G, "%s = insertvalue { ptr, ptr } undef, ptr %s, 0", a.s, h.s);
		emit(G, "%s = insertvalue { ptr, ptr } %s, ptr %s, 1", b.s, a.s, f.s);
		return b;
	}

	case EXPR_INIT: {
		/* an object built by an initializer */
		Op slot = entry_alloca(G, "ptr", "obj");
		gen_into(G, e, slot);
		Op r = tmp(G);
		emit(G, "%s = load ptr, ptr %s", r.s, slot.s);
		return r;
	}

	case EXPR_STRING:
	case EXPR_SIZEOF:
		break;
	}
	return op("undef");
}

static void gen_init(CG *G, Expr *e, Op dst)
{
	Type *t = e->ty;
	Expr **slots = e->as.init.slots;
	if (t->kind == TY_LOOKUP) {
		memset0(G, dst, type_size(t));   /* (64 bytes, or 48 where a pointer is 4) */
		for (int i = 0; i < e->as.init.nslots; i += 2) {
			Op src = entry_alloca(G, llty(G, t->inner), "val");
			gen_into(G, slots[i + 1], src);
			lk_store(G, t, dst, slots[i], src);
		}
		return;
	}
	if (t->kind == TY_LIST) {
		/* an empty list, room for every element, then each moved in */
		int n = e->as.init.nslots;
		memset0(G, dst, 24);
		if (n == 0)
			return;
		emit(G, "call void @cby_list_reserve(ptr %s, i64 %s, i64 %d)", dst.s, elem_size(G, t->inner), n);
		Op dp = tmp(G), d = tmp(G), lp = tmp(G);
		emit(G, "%s = getelementptr %%list, ptr %s, i32 0, i32 0", dp.s, dst.s);
		emit(G, "%s = load ptr, ptr %s", d.s, dp.s);
		for (int i = 0; i < n; i++) {
			Op p = tmp(G);
			emit(G, "%s = getelementptr %s, ptr %s, i64 %d", p.s, llty(G, t->inner), d.s, i);
			gen_into(G, slots[i], p);
		}
		emit(G, "%s = getelementptr %%list, ptr %s, i32 0, i32 1", lp.s, dst.s);
		emit(G, "store i64 %d, ptr %s", n, lp.s);
		return;
	}
	if (t->kind == TY_ARRAY) {
		for (uint64_t i = 0; i < t->len; i++) {
			Op p = tmp(G);
			emit(G, "%s = getelementptr %s, ptr %s, i64 0, i64 %" PRIu64, p.s, llty(G, t), dst.s, i);
			if (slots[i])
				gen_into(G, slots[i], p);
			else
				emit_zero(G, t->inner, p, false);
		}
		return;
	}
	ObjectInfo *o = t->obj;
	if (o->kind == OBJ_UNION) {
		memset0(G, dst, o->size);
		for (int i = 0; i < o->field_count; i++)
			if (slots[i])
				gen_into(G, slots[i], dst);
		return;
	}
	if (o->kind == OBJ_STRUCT) {
		for (int i = 0; i < o->field_count; i++) {
			Op p = tmp(G);
			emit(G, "%s = getelementptr %%S.%d, ptr %s, i32 0, i32 %d", p.s, o->id, dst.s, i);
			if (slots[i])
				gen_into(G, slots[i], p);
			else
				emit_zero(G, o->fields[i].type, p, false);
		}
		return;
	}
	/* a field's value that might panic (a call, say) is worked out first,
	 * into a temporary of its own, so a panic an else catches drops what
	 * was made and no object is left half made */
	Op *staged = arena_alloc(&G->A, sizeof(Op) * (size_t)(o->field_count ? o->field_count : 1));
	for (int i = 0; i < o->field_count; i++) {
		staged[i].s[0] = 0;
		Expr *x = slots[i];
		if (!x || x->kind == EXPR_INT || x->kind == EXPR_FLOAT || x->kind == EXPR_BOOL || x->kind == EXPR_CHAR ||
		    x->kind == EXPR_STRING || x->kind == EXPR_IDENT || x->kind == EXPR_SELF)
			continue;
		Type *ft = o->fields[i].type;
		staged[i] = new_temp_slot(G, ft, true);
		gen_into(G, x, staged[i]);
		temp_initialized(G, staged[i]);
	}
	Op h = tmp(G);
	/* malloc, then zero in place: calloc misses glibc's fast cache, and
	 * LLVM drops the zeroing of fields that are set straight after */
	need_intrinsic(G, "llvm.memset", "declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)");
	gen_obj_alloc(G, h, o->id);
	emit(G, "call void @llvm.memset.p0.i64(ptr %s, i8 0, i64 ptrtoint (ptr getelementptr (%%O.%d, ptr null, i32 1) to i64), i1 false)",
	     h.s, o->id);
	emit(G, "store ptr @\"ci.%d\", ptr %s", o->id, h.s);
	for (int i = 0; i < o->field_count; i++) {
		Op p = obj_field_static(G, o, h, i);
		if (staged[i].s[0]) {
			/* moved in from its temporary, which then holds nothing */
			Type *ft = o->fields[i].type;
			need_intrinsic(G, "llvm.memcpy", "declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)");
			emit(G, "call void @llvm.memcpy.p0.p0.i64(ptr %s, ptr %s, i64 %" PRIu64 ", i1 false)", p.s, staged[i].s,
			     size_of(ft));
			for (int k = G->temps.len - 1; k >= 0; k--)
				if (strcmp(G->temps.items[k].slot, staged[i].s) == 0) {
					set_flag(G, G->temps.items[k].flag, 0);
					break;
				}
		} else if (slots[i]) {
			gen_into(G, slots[i], p);
		} else {
			emit_zero(G, o->fields[i].type, p, true);
		}
	}
	if (o->has_maker)
		emit(G, "call void @\"made.%d\"(ptr %s)", o->id, h.s);   /* once its fields are set */
	emit(G, "store ptr %s, ptr %s", h.s, dst.s);
}

/* Evaluates `e` into the (uninitialized) memory at dst. */
static void gen_into(CG *G, Expr *e, Op dst)
{
	Type *t = e->ty;
	if (!is_agg(t) || e->kind == EXPR_INIT) {
		if (e->kind == EXPR_INIT) {
			gen_init(G, e, dst);
			return;
		}
		if (t->kind == TY_VOID) {
			gen_val(G, e);
			return;
		}
		store(G, t, gen_val(G, e), dst, false);
		return;
	}
	switch (e->kind) {
	case EXPR_STRING: {
		int id = string_literal_id(G, e->as.str.data, e->as.str.len);
		if (e->to_owned_str) {
			emit(G, "call void @cby_str_dup(ptr %s, ptr @.str.%d, i64 %zu)", dst.s, id, e->as.str.len);
		} else {
			Op d = tmp(G), l = tmp(G);
			emit(G, "%s = getelementptr %%str, ptr %s, i32 0, i32 0", d.s, dst.s);
			emit(G, "store ptr @.str.%d, ptr %s", id, d.s);
			emit(G, "%s = getelementptr %%str, ptr %s, i32 0, i32 1", l.s, dst.s);
			emit(G, "store i64 %" PRId64 ", ptr %s", (int64_t)e->as.str.len | STR_STATIC_BIT, l.s);
		}
		return;
	}
	case EXPR_IDENT:
	case EXPR_SELF:
	case EXPR_FIELD:
	case EXPR_INDEX: {
		Op src = gen_place(G, e);
		memcpy_(G, dst, src, size_of(t));
		if (e->kind == EXPR_IDENT && e->use == USE_MOVE) {
			Symbol *s = e->as.ident.sym;
			if (s->kind != SYM_GLOBAL && G->flag[s->index])
				set_flag(G, G->flag[s->index], 0);
		}
		return;
	}
	case EXPR_CALL:
		gen_call(G, e, &dst);
		return;
	case EXPR_CAST:
		gen_into(G, e->as.cast.value, dst);
		return;
	case EXPR_BORROW: {
		/* a slice: { data, len } */
		Expr *o = e->as.borrow.operand;
		Op src = gen_place(G, o);
		Op d = tmp(G), l = tmp(G);
		emit(G, "%s = getelementptr %%slice, ptr %s, i32 0, i32 0", d.s, dst.s);
		emit(G, "%s = getelementptr %%slice, ptr %s, i32 0, i32 1", l.s, dst.s);
		if (o->ty->kind == TY_SLICE) {
			memcpy_(G, dst, src, 16);
		} else {
			emit(G, "store ptr %s, ptr %s", src.s, d.s);
			emit(G, "store i64 %" PRIu64 ", ptr %s", o->ty->len, l.s);
		}
		return;
	}
	default:
		store(G, t, gen_val(G, e), dst, false);
		return;
	}
}

/* ---- calls ---- */

/* One value for print and println: a str or a list by its address, anything else as a value. */
static void print_one(CG *G, Type *t, Op v)
{
	switch (t->kind) {
	case TY_STR:
		emit(G, "call void @cby_print_str(ptr %s)", v.s);
		break;
	case TY_LIST:
		emit(G, "call void @\"lprint.%d\"(ptr %s)", list_id(G, t->inner), v.s);
		break;
	case TY_LOOKUP:
		emit(G, "call void @\"kprint.%d\"(ptr %s)", lookup_id(G, t), v.s);
		break;
	case TY_BOOL:
		emit(G, "call void @rt_print_bool(i1 zeroext %s)", v.s);
		break;
	case TY_FLOAT:
		if (t->bits == 32)
			emit(G, "call void @rt_print_float(float %s)", v.s);
		else
			emit(G, "call void @rt_print_double(double %s)", v.s);
		break;
	case TY_INT:
		if (t->is_char) {
			emit(G, "call void @rt_print_char(i8 zeroext %s)", v.s);
		} else {
			Op w = to_i64(G, v, t);
			emit(G, "call void @%s(i64 %s)", t->is_signed ? "rt_print_int" : "rt_print_uint", w.s);
		}
		break;
	default:
		break;
	}
}

static void gen_print(CG *G, Expr *e)
{
	ExprVec *args = &e->as.call.args;
	int n = args->len;
	Op *vals = arena_alloc(&G->A, sizeof(Op) * (size_t)(n ? n : 1));
	/* evaluate everything first; numbers are captured as values */
	for (int i = 0; i < n; i++) {
		Expr *a = args->items[i];
		vals[i] = a->ty->kind == TY_STR || a->ty->kind == TY_LIST || a->ty->kind == TY_LOOKUP ? gen_read(G, a)
											  : gen_val(G, a);
	}
	for (int i = 0; i < n; i++) {
		Type *t = args->items[i]->ty;
		if (i)
			emit(G, "call void @rt_print_sep()");
		print_one(G, t, vals[i]);
	}
	if (e->as.call.builtin == BUILTIN_PRINTLN)
		emit(G, "call void @rt_print_newline()");
}

static const char *c_param_attr(const Type *t)
{
	if (t->kind == TY_BOOL)
		return " zeroext";
	if (t->kind == TY_INT && t->bits < 32)
		return t->is_signed ? " signext" : " zeroext";
	return "";
}

static bool c_long32(const FnDecl *fn, const Type *t);

static const char *extern_sig(CG *G, FnDecl *fn)
{
	char buf[1024];
	size_t n = 0;
	for (int i = 0; i < fn->params.len; i++) {
		Type *t = fn->params.items[i]->type->resolved;
		const char *ty = (t->kind == TY_STR || t->kind == TY_REF) ? "ptr" : t->kind == TY_HANDLER ? "ptr, ptr"
				 : c_long32(fn, t) ? "i32" : vty(G, t);
		n += (size_t)snprintf(buf + n, sizeof buf - n, "%s%s%s", i ? ", " : "", ty, c_param_attr(t));
	}
	if (fn->is_variadic)
		n += (size_t)snprintf(buf + n, sizeof buf - n, "%s...", fn->params.len ? ", " : "");
	return arena_strndup(&G->A, buf, n);
}

/* an extern function's C name (C's printf is Cabby's, which follows stderr
 * and the files a call is sent to) */
static const char *c_name(const FnDecl *fn)
{
	if (strcmp(fn->name, "printf") == 0)
		return "cby_printf";
	/* (in WebAssembly a C long is 32 bits: these, as their long long twins,
	 * keep a Cabby long's 64) */
	static const char *const twins[][2] = { { "labs", "llabs" }, { "atol", "atoll" }, { "strtol", "strtoll" },
						{ "strtoul", "strtoull" } };
	if (codegen_wasm)
		for (size_t i = 0; i < sizeof twins / sizeof *twins; i++)
			if (strcmp(fn->name, twins[i][0]) == 0)
				return twins[i][1];
	return fn->name;
}

/* A Cabby long that's a C long or size_t, 32 bits in WebAssembly: one of
 * C's own functions (not Cabby's runtime, whose are 64), other than those
 * whose C type is 64 bits there too (long long, time_t, off_t) */
static bool c_long32(const FnDecl *fn, const Type *t)
{
	if (!codegen_wasm || t->kind != TY_INT || t->bits != 64)
		return false;
	const char *n = c_name(fn);
	if (strncmp(n, "cby_", 4) == 0 || strncmp(n, "rt_", 3) == 0)
		return false;
	static const char *const wide[] = { "llabs", "atoll", "strtoll", "strtoull", "time", "difftime", "lseek",
					    "clock", "imaxabs", "mktime", "timegm", NULL };
	for (const char *const *w = wide; *w; w++)
		if (strcmp(n, *w) == 0)
			return false;
	return true;
}

static void declare_extern(CG *G, FnDecl *fn)
{
	if (!mark_declared(G, astr(G, "extern.%s", fn->name)))
		return;
	Type *r = fn->ret_type;
	out(G->decl, "declare%s %s @%s(%s)\n", c_param_attr(r),
	    r->kind == TY_VOID ? "void" : c_long32(fn, r) ? "i32" : vty(G, r), c_name(fn), extern_sig(G, fn));
}

static Op gen_extern_call(CG *G, Expr *e, FnDecl *fn)
{
	declare_extern(G, fn);
	ExprVec *args = &e->as.call.args;
	char buf[2048];
	size_t n = 0;
	for (int i = 0; i < args->len; i++) {
		Expr *a = args->items[i];
		bool fixed = i < fn->params.len;
		Type *pt = fixed ? fn->params.items[i]->type->resolved : a->ty;
		Op v;
		const char *ty;
		if (pt->kind == TY_STR || (!fixed && a->ty->kind == TY_STR)) {
			Op s = gen_read(G, a);
			v = tmp(G);
			emit(G, "%s = call ptr @cby_str_cstr(ptr %s)", v.s, s.s);
			ty = "ptr";
		} else if (pt->kind == TY_REF) {
			v = gen_val(G, a);   /* EXPR_BORROW: the place itself */
			ty = "ptr";
		} else if (pt->kind == TY_HANDLER) {
			/* C gets two pointers: the window and the method */
			Op h = gen_val(G, a), o = tmp(G), f = tmp(G);
			emit(G, "%s = extractvalue { ptr, ptr } %s, 0", o.s, h.s);
			emit(G, "%s = extractvalue { ptr, ptr } %s, 1", f.s, h.s);
			v = op("%s, ptr %s", o.s, f.s);
			ty = "ptr";
		} else if (fixed && c_long32(fn, pt)) {
			/* (a C long, 32 bits: the Cabby long cut to it) */
			Op w = gen_val(G, a);
			v = tmp(G);
			emit(G, "%s = trunc i64 %s to i32", v.s, w.s);
			ty = "i32";
		} else {
			v = gen_val(G, a);
			ty = vty(G, a->ty);
		}
		n += (size_t)snprintf(buf + n, sizeof buf - n, "%s%s%s %s", i ? ", " : "", ty,
				      fixed ? c_param_attr(pt) : "", v.s);
	}
	Type *r = fn->ret_type;
	bool narrow = c_long32(fn, r);
	const char *rt = r->kind == TY_VOID ? "void" : narrow ? "i32" : vty(G, r);
	const char *callee = astr(G, "%s (%s)", rt, extern_sig(G, fn));
	if (r->kind == TY_VOID) {
		emit(G, "call %s @%s(%.*s)", callee, c_name(fn), (int)n, buf);
		return op("undef");
	}
	Op res = tmp(G);
	emit(G, "%s = call %s @%s(%.*s)", res.s, callee, c_name(fn), (int)n, buf);
	if (narrow) {
		/* (and a C long back to a Cabby long) */
		Op w = tmp(G);
		emit(G, "%s = %s i32 %s to i64", w.s, r->is_signed ? "sext" : "zext", res.s);
		return w;
	}
	return res;
}

/* Signature of a Cabby function, as a function type (for indirect calls). */
static const char *fn_type(CG *G, FnDecl *fn)
{
	char buf[1024];
	size_t n = 0;
	Type *r = fn->ret_type;
	bool sret = is_agg(r);
	n += (size_t)snprintf(buf + n, sizeof buf - n, "%s (", sret || r->kind == TY_VOID ? "void" : vty(G, r));
	bool first = true;
	if (sret) {
		n += (size_t)snprintf(buf + n, sizeof buf - n, "ptr");
		first = false;
	}
	if (fn->self_kind != SELF_NONE && fn->owner) {
		n += (size_t)snprintf(buf + n, sizeof buf - n, "%sptr", first ? "" : ", ");
		first = false;
	}
	for (int i = 0; i < fn->params.len; i++) {
		Type *t = fn->params.items[i]->type->resolved;
		n += (size_t)snprintf(buf + n, sizeof buf - n, "%s%s", first ? "" : ", ", is_agg(t) ? "ptr" : vty(G, t));
		first = false;
	}
	n += (size_t)snprintf(buf + n, sizeof buf - n, ")");
	return arena_strndup(&G->A, buf, n);
}

/* a header (.hby): a library's, or the program's own */
static bool is_header_file(int file)
{
	const char *n = file >= 0 ? diag_file_name(file) : NULL;
	size_t len = n ? strlen(n) : 0;
	return len > 4 && strcmp(n + len - 4, ".hby") == 0;
}

static Op gen_call(CG *G, Expr *e, Op *sret)
{
	if (e->as.call.builtin > BUILTIN_PRINTLN)
		return gen_str_builtin(G, e, sret);
	if (e->as.call.builtin) {
		gen_print(G, e);
		return op("undef");
	}
	FnDecl *fn = e->as.call.fn;
	if (fn->definition)
		fn = fn->definition;
	if (fn->is_extern) {
		Op r = gen_extern_call(G, e, fn);
		if (sret)
			store(G, e->ty, r, *sret, false);
		return r;
	}

	ExprVec *args = &e->as.call.args;
	char buf[4096];
	size_t n = 0;
	Type *rt = fn->ret_type;
	bool by_sret = is_agg(rt);
	Op ret_slot = { "" };
	if (by_sret) {
		ret_slot = sret ? *sret : new_temp_slot(G, rt, true);
		n += (size_t)snprintf(buf + n, sizeof buf - n, "ptr %s", ret_slot.s);
	}
	/* arguments first, left to right */
	Op *vals = arena_alloc(&G->A, sizeof(Op) * (size_t)(args->len ? args->len : 1));
	for (int i = 0; i < args->len; i++) {
		Expr *a = args->items[i];
		Type *pt = fn->params.items[i]->type->resolved;
		if (is_agg(pt)) {
			Op slot = entry_alloca(G, llty(G, pt), "arg");
			gen_into(G, a, slot);
			vals[i] = slot;   /* ownership passes to the callee */
		} else {
			vals[i] = gen_val(G, a);
		}
	}
	/* then the receiver */
	Op self = { "" };
	Expr *callee = e->as.call.callee;
	bool has_self = callee->kind == EXPR_FIELD && !e->as.call.is_static;
	if (has_self) {
		Op p = gen_read(G, callee->as.field.object);
		self = tmp(G);
		emit(G, "%s = load ptr, ptr %s", self.s, p.s);
		if (e->as.call.worker_check)
			emit(G, "call void @rt_worker_check(ptr %s, ptr %s, i32 %d, i32 %d)", self.s, file_ref(G, e->loc).s,
			     e->loc.line, e->loc.col);
		n += (size_t)snprintf(buf + n, sizeof buf - n, "%sptr %s", n ? ", " : "", self.s);
	}
	for (int i = 0; i < args->len; i++) {
		Type *pt = fn->params.items[i]->type->resolved;
		n += (size_t)snprintf(buf + n, sizeof buf - n, "%s%s %s", n ? ", " : "", is_agg(pt) ? "ptr" : vty(G, pt),
				      vals[i].s);
	}
	const char *target;
	if (has_self && e->as.call.is_virtual && e->as.call.slot >= 0) {
		Op ci = tmp(G), vt = tmp(G), fp = tmp(G), f = tmp(G);
		emit(G, "%s = load ptr, ptr %s", ci.s, self.s);
		emit(G, "%s = load ptr, ptr %s", vt.s, ci.s);
		emit(G, "%s = getelementptr ptr, ptr %s, i64 %d", fp.s, vt.s, e->as.call.slot);
		emit(G, "%s = load ptr, ptr %s", f.s, fp.s);
		target = astr(G, "%s", f.s);
	} else {
		target = fn_name(G, fn);
	}
	const char *ftype = fn_type(G, fn);
	/* the program calling into a library: where, for a panic inside it */
	bool lib_call = is_header_file(fn->loc.file) && !is_header_file(e->loc.file);
	if (lib_call)
		emit(G, "call void @rt_set_lib_call(ptr %s, i32 %d, i32 %d)", file_ref(G, e->loc).s, e->loc.line, e->loc.col);
	if (by_sret || rt->kind == TY_VOID) {
		emit(G, "call %s %s(%.*s)", ftype, target, (int)n, buf);
		if (lib_call)
			emit(G, "call void @rt_set_lib_call(ptr null, i32 0, i32 0)");
		if (by_sret && !sret)
			temp_initialized(G, ret_slot);
		return ret_slot;
	}
	Op r = tmp(G);
	emit(G, "%s = call %s %s(%.*s)", r.s, ftype, target, (int)n, buf);
	if (lib_call)
		emit(G, "call void @rt_set_lib_call(ptr null, i32 0, i32 0)");
	if (sret)
		store(G, rt, r, *sret, false);
	return r;
}

/* ============================================================== statements */

static void gen_block(CG *G, Block *b, int kind);
static void gen_stmt(CG *G, Stmt *s);

static Op gen_cond(CG *G, Expr *e)
{
	int mark = G->temps.len;
	Op v = gen_val(G, e);
	release_temps(G, mark);
	return v;
}

static void gen_decl(CG *G, VarDecl *v)
{
	Symbol *s = v->sym;
	if (s->kind == SYM_GLOBAL)
		return;   /* static locals are globals */
	Op slot = op("%s", G->slot[s->index]);
	if (s->type->kind == TY_REF) {
		Op p = gen_val(G, v->init);
		emit(G, "store ptr %s, ptr %s", p.s, slot.s);
		return;
	}
	if (v->init)
		gen_into(G, v->init, slot);
	else
		emit_zero(G, s->type, slot, false);
	set_flag(G, G->flag[s->index], 1);
}

static Op label_of(CG *G, Stmt *label)
{
	for (int i = 0; i < G->labels.len; i++)
		if (G->labels.items[i].stmt == label)
			return op("%s", G->labels.items[i].name);
	LabelName ln = { label, "" };
	snprintf(ln.name, sizeof ln.name, "L%d.label.%s", ++G->nlbl, label->as.label.name);
	vec_push(&G->A, G->labels, ln);
	return op("%s", ln.name);
}

static int find_scope_kind(CG *G, bool loop_only)
{
	for (int i = G->nscopes - 1; i >= 0; i--)
		if (G->scopes[i].kind == SC_LOOP || (!loop_only && G->scopes[i].kind == SC_SWITCH))
			return i;
	return -1;
}

/* where the function returns: its frame told it's gone, if it was told
 * it began (a marker for now, filled in once the function's done) */
#define FRAME_LEAVE_MARK "\t;cabby.frame.leave\n"

static void frame_leave_here(CG *G)
{
	if (G->frames_on && !G->term)
		out(G->body, "%s", FRAME_LEAVE_MARK);
}

static void gen_return(CG *G, Stmt *s)
{
	Expr *v = s->as.ret.value;
	Type *rt = G->fn->ret_type;
	Op val = { "" };
	if (v) {
		if (G->sret[0])
			gen_into(G, v, op("%s", G->sret));
		else
			val = gen_val(G, v);
	}
	release_temps(G, 0);
	cleanup_to(G, 0);
	frame_leave_here(G);
	if (G->fn == G->S->main_fn && rt->kind != TY_VOID && !v)
		emit_term(G, "ret i32 0");
	else if (G->sret[0] || rt->kind == TY_VOID)
		emit_term(G, "ret void");
	else
		emit_term(G, "ret %s %s", vty(G, rt), val.s);
}

static void gen_switch(CG *G, Stmt *s)
{
	int mark = G->temps.len;
	Op v = gen_val(G, s->as.switch_.value);
	release_temps(G, mark);
	Type *vt = s->as.switch_.value->ty;
	CaseVec *cases = &s->as.switch_.cases;
	Op done = lbl(G, "sw.end");
	Op *bodies = arena_alloc(&G->A, sizeof(Op) * (size_t)(cases->len ? cases->len : 1));
	for (int i = 0; i < cases->len; i++)
		bodies[i] = lbl(G, "case");
	Op dflt = s->as.switch_.default_index >= 0 ? bodies[s->as.switch_.default_index] : done;
	char buf[8192];
	size_t n = 0;
	for (int i = 0; i < cases->len; i++)
		for (int j = 0; j < cases->items[i].values.len; j++)
			n += (size_t)snprintf(buf + n, sizeof buf - n, " %s %" PRId64 ", label %%%s", vty(G, vt),
					      norm(cases->items[i].consts[j], vt), bodies[i].s);
	emit_term(G, "switch %s %s, label %%%s [%.*s ]", vty(G, vt), v.s, dflt.s, (int)n, buf);
	CScope *sc = push_scope(G, SC_SWITCH, NULL, NULL);
	snprintf(sc->brk, sizeof sc->brk, "%s", done.s);
	for (int i = 0; i < cases->len; i++) {
		place_label(G, bodies[i]);
		gen_block(G, cases->items[i].body, SC_BLOCK);
		/* falls through into the next case */
	}
	G->nscopes--;
	place_label(G, done);
}

static void gen_loop_body(CG *G, Block *body, Op brk, Op cont)
{
	CScope *sc = push_scope(G, SC_LOOP, NULL, NULL);
	snprintf(sc->brk, sizeof sc->brk, "%s", brk.s);
	snprintf(sc->cont, sizeof sc->cont, "%s", cont.s);
	gen_block(G, body, SC_BLOCK);
	G->nscopes--;
}

/* `... else`: the statement, with a panic in it caught (the runtime's
 * cby_try_begin, then _setjmp: 0 now, 1 when a panic jumps back), and
 * what's done instead if it was */
static void gen_recovering(CG *G, Stmt *s)
{
	Recover *r = s->recover;
	int mark = G->temps.len;
	Op t = tmp(G), j = tmp(G), ok = tmp(G);
	Op body = lbl(G, "try"), fail = lbl(G, "caught"), done = lbl(G, "tried");
	emit(G, "%s = call ptr @cby_try_begin()", t.s);
	emit(G, "%s = call i32 @%s(ptr %s)", j.s, codegen_wasm ? "setjmp" : "_setjmp", t.s);   /* (WebAssembly's is setjmp) */
	emit(G, "%s = icmp eq i32 %s, 0", ok.s, j.s);
	emit_term(G, "br i1 %s, label %%%s, label %%%s", ok.s, body.s, fail.s);
	place_label(G, body);
	s->recover = NULL;
	G->try_depth++;
	bool kept = G->keep_released;
	int nkept = G->released.len;
	G->keep_released = true;
	gen_stmt(G, s);
	G->keep_released = kept;
	s->recover = r;
	emit(G, "call void @cby_try_end(ptr %s)", t.s);
	br(G, done);
	place_label(G, fail);
	emit(G, "call void @cby_try_failed(ptr %s)", t.s);
	/* the statement's own temporaries: those it had made, by their flags
	 * (written volatile, so they're as the panic left them) */
	G->temps.len = mark;
	for (int i = G->released.len - 1; i >= nkept; i--)
		vec_push(&G->A, G->temps, G->released.items[i]);
	G->released.len = nkept;
	release_temps(G, mark);
	G->try_depth--;
	if (s->kind == STMT_DECL && r->value) {
		VarDecl *v = s->as.decl.items[0];
		Expr *init = v->init;
		v->init = r->value;
		gen_decl(G, v);
		v->init = init;
		release_temps(G, mark);
	} else if (r->block) {
		gen_block(G, r->block, SC_BLOCK);
	}
	br(G, done);
	place_label(G, done);
}

static void gen_stmt(CG *G, Stmt *s)
{
	int mark = G->temps.len;
	if (s->recover && (s->kind == STMT_DECL || s->kind == STMT_EXPR)) {
		gen_recovering(G, s);
		return;
	}
	switch (s->kind) {
	case STMT_DECL:
		for (int i = 0; i < s->as.decl.len; i++)
			gen_decl(G, s->as.decl.items[i]);
		break;
	case STMT_EXPR: {
		Expr *e = s->as.expr;
		if (e->kind == EXPR_CALL && e->ty->kind != TY_VOID) {
			/* an unused result is dropped right away */
			if (is_agg(e->ty)) {
				gen_call(G, e, NULL);   /* lands in a temp, dropped below */
			} else {
				Op r = gen_call(G, e, NULL);
				if (is_obj(e->ty)) {
					Op slot = new_temp_slot(G, e->ty, true);
					emit(G, "store ptr %s, ptr %s", r.s, slot.s);
					temp_initialized(G, slot);
				}
			}
		} else if (e->kind == EXPR_ASSIGN) {
			gen_assign(G, e);
		} else if (e->kind == EXPR_CALL) {
			gen_call(G, e, NULL);
		} else if (!is_agg(e->ty) && e->ty->kind != TY_VOID) {
			gen_val(G, e);
		} else {
			gen_read(G, e);
		}
		break;
	}
	case STMT_IF: {
		Op c = gen_cond(G, s->as.if_.cond);
		Op then = lbl(G, "then"), els = lbl(G, "else"), done = lbl(G, "endif");
		emit_term(G, "br i1 %s, label %%%s, label %%%s", c.s, then.s, s->as.if_.else_branch ? els.s : done.s);
		place_label(G, then);
		gen_block(G, s->as.if_.then_block, SC_BLOCK);
		br(G, done);
		if (s->as.if_.else_branch) {
			place_label(G, els);
			gen_stmt(G, s->as.if_.else_branch);
			br(G, done);
		}
		place_label(G, done);
		break;
	}
	case STMT_BLOCK:
		gen_block(G, s->as.block, SC_BLOCK);
		break;
	case STMT_WHILE: {
		Op head = lbl(G, "while"), body = lbl(G, "body"), done = lbl(G, "wend");
		br(G, head);
		place_label(G, head);
		Op c = gen_cond(G, s->as.while_.cond);
		emit_term(G, "br i1 %s, label %%%s, label %%%s", c.s, body.s, done.s);
		place_label(G, body);
		gen_loop_body(G, s->as.while_.body, done, head);
		br(G, head);
		place_label(G, done);
		break;
	}
	case STMT_DO: {
		Op body = lbl(G, "do"), cond = lbl(G, "docond"), done = lbl(G, "doend");
		br(G, body);
		place_label(G, body);
		gen_loop_body(G, s->as.while_.body, done, cond);
		br(G, cond);
		place_label(G, cond);
		Op c = gen_cond(G, s->as.while_.cond);
		emit_term(G, "br i1 %s, label %%%s, label %%%s", c.s, body.s, done.s);
		place_label(G, done);
		break;
	}
	case STMT_FOR: {
		push_scope(G, SC_BLOCK, s->as.for_.scope, &s->as.for_.scope->locals);
		int scope_index = G->nscopes - 1;
		if (s->as.for_.init)
			gen_stmt(G, s->as.for_.init);
		Op head = lbl(G, "for"), body = lbl(G, "fbody"), upd = lbl(G, "fupd"), done = lbl(G, "fend");
		br(G, head);
		place_label(G, head);
		if (s->as.for_.cond) {
			Op c = gen_cond(G, s->as.for_.cond);
			emit_term(G, "br i1 %s, label %%%s, label %%%s", c.s, body.s, done.s);
		} else {
			br(G, body);
		}
		place_label(G, body);
		gen_loop_body(G, s->as.for_.body, done, upd);
		br(G, upd);
		place_label(G, upd);
		if (s->as.for_.update)
			gen_stmt(G, s->as.for_.update);
		br(G, head);
		place_label(G, done);
		cleanup_to(G, scope_index);
		G->nscopes = scope_index;
		break;
	}
	case STMT_SWITCH:
		gen_switch(G, s);
		break;
	case STMT_RETURN:
		gen_return(G, s);
		break;
	case STMT_BREAK:
	case STMT_CONTINUE: {
		int target = find_scope_kind(G, s->kind == STMT_CONTINUE);
		cleanup_to(G, target + 1);
		br(G, op("%s", s->kind == STMT_BREAK ? G->scopes[target].brk : G->scopes[target].cont));
		break;
	}
	case STMT_GOTO: {
		Block *tb = s->as.goto_.target_block;
		int keep = 0;
		for (int i = G->nscopes - 1; i >= 0; i--)
			if (G->scopes[i].block == tb) {
				keep = i + 1;
				break;
			}
		cleanup_to(G, keep);
		br(G, label_of(G, s->as.goto_.target));
		break;
	}
	case STMT_LABEL:
		place_label(G, label_of(G, s));
		break;
	}
	release_temps(G, mark);
}

static void gen_block(CG *G, Block *b, int kind)
{
	push_scope(G, kind, b, &b->locals);
	int index = G->nscopes - 1;
	for (int i = 0; i < b->stmts.len; i++)
		gen_stmt(G, b->stmts.items[i]);
	if (!G->term)
		cleanup_to(G, index);
	G->nscopes = index;
}

/* ============================================================== functions */

static void begin_function(CG *G)
{
	G->entry = open_memstream(&G->entry_buf, &G->entry_len);
	G->body = open_memstream(&G->body_buf, &G->body_len);
	G->term = false;
	G->nscopes = 0;
	G->temps.len = 0;
	G->labels.len = 0;
}

static bool codegen_checked;   /* --checked */

/* --checked (or CABBY_SANITIZE=1) marks every function for AddressSanitizer:
 * main too, or what's inlined into it goes unchecked */
static const char *checked_attrs(void)
{
	/* (in WebAssembly, `else`'s setjmp is its exceptions, which a function
	 * from IR has only when it says so) */
	if (codegen_wasm)
		return " \"target-features\"=\"+exception-handling\"";
	const char *san = getenv("CABBY_SANITIZE");
	return codegen_checked || (san && *san == '1') ? " sanitize_address" : "";
}

static void end_function(CG *G, const char *header)
{
	fclose(G->entry);
	fclose(G->body);
	const char *attrs = checked_attrs();
	out(G->funcs, "%s%s {\nentry:\n%s\tbr label %%start\nstart:\n%s}\n\n", header, attrs, G->entry_buf, G->body_buf);
	free(G->entry_buf);
	free(G->body_buf);
}

static void frame_finish(CG *G);

static void gen_function(CG *G, FnDecl *fn)
{
	G->fn = fn;
	begin_function(G);
	G->fitems.len = 0;
	G->frames_on = fn != G->S->main_fn;   /* (main starts before any else can wait) */
	int n = fn->frame.len;
	G->slot = arena_alloc(&G->A, sizeof(char *) * (size_t)(n ? n : 1));
	G->flag = arena_alloc(&G->A, sizeof(char *) * (size_t)(n ? n : 1));
	for (int i = 0; i < n; i++) {
		Symbol *s = fn->frame.items[i];
		/* self's slot holds the object handle; a reference's holds a pointer */
		const char *ty = (s->kind == SYM_SELF || s->type->kind == TY_REF) ? "ptr" : llty(G, s->type);
		Op a = entry_alloca(G, ty, "v");
		G->slot[i] = arena_strndup(&G->A, a.s, strlen(a.s));
		G->flag[i] = NULL;
		if (s->kind != SYM_SELF && s->type->kind != TY_REF && type_needs_drop(s->type)) {
			Op f = new_flag(G);
			G->flag[i] = arena_strndup(&G->A, f.s, strlen(f.s));
			if (G->frames_on) {
				CTemp it = { .ty = s->type };
				snprintf(it.slot, sizeof it.slot, "%s", G->slot[i]);
				snprintf(it.flag, sizeof it.flag, "%s", f.s);
				vec_push(&G->A, G->fitems, it);
			}
		}
	}

	Type *rt = fn->ret_type;
	bool by_sret = is_agg(rt);
	G->sret[0] = '\0';
	char params[2048] = "";   /* (stays empty for a function without parameters) */
	size_t pn = 0;
	if (by_sret) {
		snprintf(G->sret, sizeof G->sret, "%%sret");
		pn += (size_t)snprintf(params + pn, sizeof params - pn, "ptr %%sret");
	}
	if (fn->self_sym) {
		pn += (size_t)snprintf(params + pn, sizeof params - pn, "%sptr %%self", pn ? ", " : "");
		out(G->entry, "\tstore ptr %%self, ptr %s\n", G->slot[fn->self_sym->index]);
	}
	SymbolVec *params_vec = arena_alloc(&G->A, sizeof *params_vec);
	for (int i = 0; i < fn->params.len; i++) {
		Symbol *s = fn->params.items[i]->sym;
		Type *t = s->type;
		const char *pty = is_agg(t) ? "ptr" : vty(G, t);
		pn += (size_t)snprintf(params + pn, sizeof params - pn, "%s%s %%p%d", pn ? ", " : "", pty, i);
		if (is_agg(t)) {
			need_intrinsic(G, "llvm.memcpy", "declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)");
			out(G->entry, "\tcall void @llvm.memcpy.p0.p0.i64(ptr %s, ptr %%p%d, i64 %" PRIu64 ", i1 false)\n",
			    G->slot[s->index], i, size_of(t));
		} else if (t->kind == TY_BOOL) {
			out(G->entry, "\t%%pz%d = zext i1 %%p%d to i8\n\tstore i8 %%pz%d, ptr %s\n", i, i, i, G->slot[s->index]);
		} else {
			out(G->entry, "\tstore %s %%p%d, ptr %s\n", t->kind == TY_REF ? "ptr" : llty(G, t), i, G->slot[s->index]);
		}
		if (G->flag[s->index])
			out(G->entry, "\tstore i8 1, ptr %s\n", G->flag[s->index]);
		vec_push(&G->A, *params_vec, s);
	}

	push_scope(G, SC_FN, NULL, params_vec);
	gen_block(G, fn->body, SC_BLOCK);
	if (!G->term) {
		cleanup_to(G, 0);
		frame_leave_here(G);
		if (fn == G->S->main_fn && rt->kind != TY_VOID)
			emit_term(G, "ret i32 0");
		else if (by_sret || rt->kind == TY_VOID)
			emit_term(G, "ret void");
		else
			emit_term(G, "unreachable");
	}
	const char *ret = by_sret || rt->kind == TY_VOID ? "void" : vty(G, rt);
	frame_finish(G);
	G->frames_on = false;
	end_function(G, astr(G, "define internal %s %s(%s)", ret, fn_name(G, fn), params));
	G->fn = NULL;
}

/* The helper that drops a value of type t (given where it is): fdrop.N */
static int fdrop_id(CG *G, Type *t)
{
	for (int i = 0; i < G->fdrops.len; i++)
		if (type_eq(G->fdrops.items[i], t))
			return i;
	vec_push(&G->A, G->fdrops, t);
	return G->fdrops.len - 1;
}

/* A function with something to drop: when it starts, if an else is
 * waiting (the runtime's cby_trying), it tells the runtime where each of
 * those is (its slot, its drop flag, and how to drop it), so a panic the
 * else catches drops what the function had; where it returns, it says
 * it's gone. A function with nothing to drop has neither. */
static void frame_finish(CG *G)
{
	fflush(G->body);
	int n = G->fitems.len;
	char *body = G->body_buf;
	size_t ml = strlen(FRAME_LEAVE_MARK);
	/* the body again, each marker made the code (or nothing) */
	char *outb = NULL;
	size_t outn = 0;
	FILE *o = open_memstream(&outb, &outn);
	const char *type = astr(G, "{ ptr, i64, [%d x [3 x ptr]] }", n ? n : 1);
	int k = 0;
	for (const char *p = body; *p;) {
		const char *m = strstr(p, FRAME_LEAVE_MARK);
		if (!m) {
			fputs(p, o);
			break;
		}
		fwrite(p, 1, (size_t)(m - p), o);
		if (n) {
			k++;
			fprintf(o, "\t%%fl.%d.top = load ptr, ptr @cby_frame_top\n"
				   "\t%%fl.%d.me = icmp eq ptr %%fl.%d.top, %%frame\n"
				   "\t%%fl.%d.prev = load ptr, ptr %%frame\n"
				   "\t%%fl.%d.next = select i1 %%fl.%d.me, ptr %%fl.%d.prev, ptr %%fl.%d.top\n"
				   "\tstore ptr %%fl.%d.next, ptr @cby_frame_top\n",
				k, k, k, k, k, k, k, k, k);
		}
		p = m + ml;
	}
	fclose(o);
	fclose(G->body);
	free(G->body_buf);
	G->body_buf = NULL;
	G->body_len = 0;
	G->body = open_memstream(&G->body_buf, &G->body_len);   /* (end_function closes it: nothing more is written) */
	fputs(outb, G->body);
	free(outb);
	if (!n)
		return;
	out(G->entry, "\t%%frame = alloca %s\n", type);
	out(G->entry, "\t%%frame.on = load i32, ptr @cby_trying\n"
		      "\t%%frame.trying = icmp ne i32 %%frame.on, 0\n"
		      "\tbr i1 %%frame.trying, label %%frame.tell, label %%start, !prof !0\n"
		      "frame.tell:\n");
	out(G->entry, "\t%%frame.n = getelementptr %s, ptr %%frame, i32 0, i32 1\n\tstore i64 %d, ptr %%frame.n\n", type, n);
	for (int i = 0; i < n; i++) {
		CTemp *it = &G->fitems.items[i];
		const char *what[3] = { it->slot, it->flag, astr(G, "@\"fdrop.%d\"", fdrop_id(G, it->ty)) };
		for (int j = 0; j < 3; j++)
			out(G->entry, "\t%%frame.%d.%d = getelementptr %s, ptr %%frame, i32 0, i32 2, i32 %d, i32 %d\n"
				      "\tstore ptr %s, ptr %%frame.%d.%d\n",
			    i, j, type, i, j, what[j], i, j);
	}
	out(G->entry, "\tcall void @cby_frame_enter(ptr %%frame)\n");
}

/* fdrop.N(p): the value at p dropped */
static void gen_fdrop_helper(CG *G, int id)
{
	G->fn = NULL;
	begin_function(G);
	emit_drop(G, G->fdrops.items[id], op("%%p"));
	emit_term(G, "ret void");
	end_function(G, astr(G, "define internal void @\"fdrop.%d\"(ptr %%p)", id));
}

/* A new object's memory, h (not zeroed): malloc itself, so LLVM knows
 * what it is, with 16 bytes before h for its place among linked objects
 * (rt.c), set to none */
static void gen_obj_alloc(CG *G, Op h, int id)
{
	Op raw = tmp(G), none = tmp(G), bad = lbl(G, "nomem"), ok = lbl(G, "mem");
	/* (a program's own extern malloc is the same; a size is 32 bits in WebAssembly) */
	const char *sz = codegen_wasm ? "i32" : "i64";
	need_intrinsic(G, "extern.malloc", codegen_wasm ? "declare ptr @malloc(i32)" : "declare ptr @malloc(i64)");
	emit(G, "%s = call ptr @malloc(%s add (%s ptrtoint (ptr getelementptr (%%O.%d, ptr null, i32 1) to %s), %s 16))",
	     raw.s, sz, sz, id, sz, sz);
	emit(G, "%s = icmp eq ptr %s, null", none.s, raw.s);
	emit_term(G, "br i1 %s, label %%%s, label %%%s, !prof !0", none.s, bad.s, ok.s);
	place_label(G, bad);
	emit(G, "call void @rt_out_of_memory()");
	emit_term(G, "unreachable");
	place_label(G, ok);
	emit(G, "store i64 0, ptr %s", raw.s);
	emit(G, "%s = getelementptr i8, ptr %s, i64 16", h.s, raw.s);
}

/* Per-class drop function and zero constructor. */
static void gen_object_support(CG *G, ObjectInfo *K)
{
	/* drop.K(h): destructors (most derived first), fields, free */
	begin_function(G);
	Op h = op("%%h");
	for (int i = K->layout_len - 1; i >= 0; i--) {
		FnDecl *d = K->layout[i]->decl->drop_fn;
		if (d)
			emit(G, "call void %s(ptr %%h)", fn_name(G, d));
	}
	/* links to it are gone before its fields are (a field's destructor
	 * that follows one back finds it gone) */
	{
		/* (only an object that's been linked to has a place to give up:
		 * most never have, so it's tested here, not in a call) */
		Op lp = tmp(G), lv = tmp(G), any = tmp(G), un = lbl(G, "unlink"), go = lbl(G, "fields");
		emit(G, "%s = getelementptr i8, ptr %%h, i64 -16", lp.s);
		emit(G, "%s = load i64, ptr %s", lv.s, lp.s);
		emit(G, "%s = icmp ne i64 %s, 0", any.s, lv.s);
		emit_term(G, "br i1 %s, label %%%s, label %%%s, !prof !0", any.s, un.s, go.s);
		place_label(G, un);
		emit(G, "call void @rt_obj_unlink(ptr %%h)");
		br(G, go);
		place_label(G, go);
	}
	for (int i = 0; i < K->field_count; i++) {
		Type *ft = K->fields[i].type;
		if (!type_needs_drop(ft))
			continue;
		emit_drop(G, ft, obj_field_static(G, K, h, i));
	}
	{
		Op raw = tmp(G);
		emit(G, "%s = getelementptr i8, ptr %%h, i64 -16", raw.s);
		emit(G, "call void @free(ptr %s)", raw.s);
	}
	emit_term(G, "ret void");
	end_function(G, astr(G, "define internal void @\"drop.%d\"(ptr %%h)", K->id));

	/* new.K(): a zeroed object */
	begin_function(G);
	need_intrinsic(G, "llvm.memset", "declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)");
	gen_obj_alloc(G, op("%%h"), K->id);
	emit(G, "call void @llvm.memset.p0.i64(ptr %%h, i8 0, i64 ptrtoint (ptr getelementptr (%%O.%d, ptr null, i32 1) to i64), i1 false)",
	     K->id);
	emit(G, "store ptr @\"ci.%d\", ptr %%h", K->id);
	for (int i = 0; i < K->field_count; i++)
		if (contains_object(K->fields[i].type))
			emit_zero(G, K->fields[i].type, obj_field_static(G, K, h, i), true);
	if (K->has_maker)
		emit(G, "call void @\"made.%d\"(ptr %%h)", K->id);
	emit_term(G, "ret ptr %%h");
	end_function(G, astr(G, "define internal ptr @\"new.%d\"()", K->id));

	/* made.K(h): what runs when one is made, base-most class first */
	if (K->has_maker) {
		begin_function(G);
		for (int i = 0; i < K->layout_len; i++) {
			FnDecl *d = K->layout[i]->decl->make_fn;
			if (d)
				emit(G, "call void %s(ptr %%h)", fn_name(G, d));
		}
		emit_term(G, "ret void");
		end_function(G, astr(G, "define internal void @\"made.%d\"(ptr %%h)", K->id));
	}
}

/* ============================================================== types & globals */

static void gen_type_defs(CG *G)
{
	Sema *S = G->S;
	out(G->types, "%%str = type { ptr, i64 }\n%%list = type { ptr, i64, i64 }\n%%lookup = type { ptr, ptr, ptr, ptr, i64, i64, i64, i64 }\n%%file = type { ptr }\n%%slice = type { ptr, i64 }\n%%classinfo = type { ptr, ptr, i64, ptr }\n");
	for (int i = 0; i < S->nobjects; i++) {
		ObjectInfo *o = S->objects[i];
		if (o->kind == OBJ_STRUCT) {
			out(G->types, "%%S.%d = type { ", o->id);
			for (int f = 0; f < o->field_count; f++)
				out(G->types, "%s%s", f ? ", " : "", llty(G, o->fields[f].type));
			out(G->types, " }\n");
		} else if (o->kind == OBJ_UNION) {
			uint64_t a = o->align ? o->align : 1;
			out(G->types, "%%U.%d = type { [%" PRIu64 " x i%" PRIu64 "] }\n", o->id, o->size / a, a * 8);
		} else {
			/* the class's own block, then the full object */
			out(G->types, "%%B.%d = type { ", o->id);
			int own = 0;
			for (int f = 0; f < o->field_count; f++)
				if (o->fields[f].owner == o)
					out(G->types, "%s%s", own++ ? ", " : "", llty(G, o->fields[f].type));
			out(G->types, " }\n");
		}
	}
	for (int i = 0; i < S->nobjects; i++) {
		ObjectInfo *o = S->objects[i];
		if (o->kind != OBJ_OBJECT)
			continue;
		out(G->types, "%%O.%d = type { ptr", o->id);
		for (int b = 0; b < o->layout_len; b++)
			out(G->types, ", %%B.%d", o->layout[b]->id);
		out(G->types, " }\n");
	}
}

static void gen_classinfo(CG *G)
{
	Sema *S = G->S;
	for (int i = 0; i < S->nobjects; i++) {
		ObjectInfo *o = S->objects[i];
		if (o->kind != OBJ_OBJECT)
			continue;
		out(G->glob, "@\"vt.%d\" = private constant [%d x ptr] [", o->id, S->nslots);
		for (int s = 0; s < S->nslots; s++)
			out(G->glob, "%sptr %s", s ? ", " : "", o->vtable[s] ? fn_name(G, o->vtable[s]) : "null");
		out(G->glob, "]\n");
		out(G->glob, "@\"off.%d\" = private constant [%d x i64] [", o->id, S->nobjects);
		for (int k = 0; k < S->nobjects; k++) {
			int b = -1;
			for (int j = 0; j < o->layout_len; j++)
				if (o->layout[j]->id == k)
					b = j;
			if (b < 0)
				out(G->glob, "%si64 -1", k ? ", " : "");
			else
				out(G->glob, "%si64 ptrtoint (ptr getelementptr (%%O.%d, ptr null, i32 0, i32 %d) to i64)",
				    k ? ", " : "", o->id, b + 1);
		}
		out(G->glob, "]\n");
		out(G->glob, "@\"ci.%d\" = private constant %%classinfo { ptr @\"vt.%d\", ptr @\"off.%d\", "
		    "i64 ptrtoint (ptr getelementptr (%%O.%d, ptr null, i32 1) to i64), ptr @\"drop.%d\" }\n", o->id, o->id,
		    o->id, o->id, o->id);
	}
}

/* ---- constant initializers for globals ---- */

typedef struct { bool is_float; int64_t i; double f; } CVal;

static CVal cval(CG *G, Expr *e);

static CVal cval_cast(CVal v, Type *from, Type *to)
{
	CVal r = { 0 };
	if (to->kind == TY_FLOAT) {
		r.is_float = true;
		r.f = v.is_float ? v.f : (from->kind == TY_INT && !from->is_signed) ? (double)(uint64_t)v.i : (double)v.i;
		if (to->bits == 32)
			r.f = (float)r.f;
		return r;
	}
	if (to->kind == TY_BOOL) {
		r.i = v.is_float ? v.f != 0 : v.i != 0;
		return r;
	}
	if (v.is_float) {
		double d = v.f;
		if (d != d)
			r.i = 0;
		else if (to->is_signed)
			r.i = d >= 9.2e18 ? INT64_MAX : d <= -9.2e18 ? INT64_MIN : (int64_t)d;
		else
			r.i = d <= 0 ? 0 : d >= 1.8e19 ? (int64_t)UINT64_MAX : (int64_t)(uint64_t)d;
		r.i = norm(r.i, to);
		return r;
	}
	r.i = norm(v.i, to);
	return r;
}

static CVal cval(CG *G, Expr *e)
{
	CVal r = { 0 };
	Type *t = e->ty;
	switch (e->kind) {
	case EXPR_INT:   r.i = norm((int64_t)e->as.int_lit.value, t); return r;
	case EXPR_CHAR:  r.i = (int8_t)e->as.char_val; return r;
	case EXPR_BOOL:  r.i = e->as.bool_val; return r;
	case EXPR_FLOAT: r.is_float = true; r.f = t->bits == 32 ? (double)(float)e->as.float_lit.value : e->as.float_lit.value; return r;
	case EXPR_IDENT: {
		Symbol *s = e->as.ident.sym;
		if (s && s->kind == SYM_GLOBAL && s->decl && s->decl->init)
			return cval(G, s->decl->init);
		return r;
	}
	case EXPR_CAST:
		return cval_cast(cval(G, e->as.cast.value), e->as.cast.value->ty, t);
	case EXPR_UNARY: {
		Expr *o = e->as.unary.operand;
		if (e->as.unary.op == UN_NEG && o->kind == EXPR_INT) {
			r.i = norm((int64_t)(0 - o->as.int_lit.value), t);
			return r;
		}
		CVal v = cval(G, o);
		if (e->as.unary.op == UN_NEG) {
			if (v.is_float) { v.f = -v.f; return v; }
			r.i = norm((int64_t)(0 - (uint64_t)v.i), t);
		} else if (e->as.unary.op == UN_NOT) {
			r.i = !v.i;
		} else {
			r.i = norm(~v.i, t);
		}
		return r;
	}
	case EXPR_BINARY: {
		BinaryOp o = e->as.binary.op;
		CVal a = cval(G, e->as.binary.lhs), b = cval(G, e->as.binary.rhs);
		Type *ot = e->as.binary.lhs->ty;
		if (a.is_float || b.is_float) {
			double x = a.f, y = b.f;
			if (ast_binop_is_comparison(o)) {
				r.i = o == BIN_EQ ? x == y : o == BIN_NE ? x != y : o == BIN_LT ? x < y : o == BIN_LE ? x <= y :
				      o == BIN_GT ? x > y : x >= y;
				return r;
			}
			r.is_float = true;
			r.f = o == BIN_ADD ? x + y : o == BIN_SUB ? x - y : o == BIN_MUL ? x * y : x / y;
			if (t->bits == 32)
				r.f = (float)r.f;
			return r;
		}
		bool uns = ot->kind == TY_INT && !ot->is_signed;
		uint64_t ua = (uint64_t)a.i, ub = (uint64_t)b.i;
		int64_t z = 0;
		switch (o) {
		case BIN_ADD: z = (int64_t)(ua + ub); break;
		case BIN_SUB: z = (int64_t)(ua - ub); break;
		case BIN_MUL: z = (int64_t)(ua * ub); break;
		case BIN_DIV: z = b.i == 0 ? 0 : uns ? (int64_t)(ua / ub) : (b.i == -1 ? (int64_t)(0 - ua) : a.i / b.i); break;
		case BIN_MOD: z = b.i == 0 ? 0 : uns ? (int64_t)(ua % ub) : (b.i == -1 ? 0 : a.i % b.i); break;
		case BIN_BIT_AND: z = a.i & b.i; break;
		case BIN_BIT_OR:  z = a.i | b.i; break;
		case BIN_BIT_XOR: z = a.i ^ b.i; break;
		case BIN_SHL: z = (int64_t)(ua << (b.i & 63)); break;
		case BIN_SHR: z = uns ? (int64_t)(ua >> (b.i & 63)) : (a.i < 0 ? ~(~a.i >> (b.i & 63)) : a.i >> (b.i & 63)); break;
		case BIN_LOG_AND: r.i = a.i && b.i; return r;
		case BIN_LOG_OR:  r.i = a.i || b.i; return r;
		case BIN_EQ: r.i = a.i == b.i; return r;
		case BIN_NE: r.i = a.i != b.i; return r;
		case BIN_LT: r.i = uns ? ua < ub : a.i < b.i; return r;
		case BIN_LE: r.i = uns ? ua <= ub : a.i <= b.i; return r;
		case BIN_GT: r.i = uns ? ua > ub : a.i > b.i; return r;
		case BIN_GE: r.i = uns ? ua >= ub : a.i >= b.i; return r;
		default: break;
		}
		r.i = norm(z, t);
		return r;
	}
	default:
		return r;
	}
}

static void const_text(CG *G, Expr *e, Type *t, FILE *f)
{
	if (!e) {
		fputs("zeroinitializer", f);   /* globals are plain data: all-zero bits */
		return;
	}
	if (t->kind == TY_STR) {
		int id = string_literal_id(G, e->as.str.data, e->as.str.len);
		out(f, "{ ptr @.str.%d, i64 %" PRId64 " }", id, (int64_t)e->as.str.len | STR_STATIC_BIT);
		return;
	}
	if (e->kind == EXPR_INIT) {
		Expr **slots = e->as.init.slots;
		if (t->kind == TY_ARRAY) {
			out(f, "[");
			for (uint64_t i = 0; i < t->len; i++) {
				out(f, "%s%s ", i ? ", " : "", llty(G, t->inner));
				const_text(G, slots[i], t->inner, f);
			}
			out(f, "]");
			return;
		}
		ObjectInfo *o = t->obj;
		if (o->kind == OBJ_UNION) {
			/* lay the one initialized field's bytes out by hand */
			unsigned char bytes[4096] = { 0 };
			for (int i = 0; i < o->field_count; i++) {
				if (!slots[i])
					continue;
				Type *ft = o->fields[i].type;
				CVal v = cval(G, slots[i]);
				if (ft->kind == TY_FLOAT && ft->bits == 32) {
					float x = (float)v.f;
					memcpy(bytes, &x, 4);
				} else if (ft->kind == TY_FLOAT) {
					memcpy(bytes, &v.f, 8);
				} else if (ft->kind == TY_INT || ft->kind == TY_BOOL) {
					memcpy(bytes, &v.i, ft->kind == TY_BOOL ? 1 : ft->bits / 8);
				}
			}
			uint64_t a = o->align ? o->align : 1;
			out(f, "{ [%" PRIu64 " x i%" PRIu64 "] [", o->size / a, a * 8);
			for (uint64_t i = 0; i < o->size / a; i++) {
				uint64_t w = 0;
				memcpy(&w, bytes + i * a, (size_t)a);
				out(f, "%si%" PRIu64 " %" PRIu64, i ? ", " : "", a * 8, w);
			}
			out(f, "] }");
			return;
		}
		out(f, "{ ");
		for (int i = 0; i < o->field_count; i++) {
			out(f, "%s%s ", i ? ", " : "", llty(G, o->fields[i].type));
			const_text(G, slots[i], o->fields[i].type, f);
		}
		out(f, " }");
		return;
	}
	CVal v = cval(G, e);
	if (t->kind == TY_FLOAT)
		out(f, "%s", fconst(v.is_float ? v.f : (double)v.i, t->bits).s);
	else if (t->kind == TY_BOOL)
		out(f, "%d", v.i ? 1 : 0);
	else
		out(f, "%" PRId64, norm(v.i, t));
}

static void gen_globals(CG *G)
{
	Sema *S = G->S;
	for (int i = 0; i < S->globals.len; i++) {
		Symbol *g = S->globals.items[i];
		Type *t = g->type;
		/* built in its own buffer: the initializer may emit string literals */
		char *buf;
		size_t len;
		FILE *f = open_memstream(&buf, &len);
		const_text(G, g->decl->init, t, f);
		fclose(f);
		out(G->glob, "%s = internal %s %s %s\n", global_name(G, g), g->is_const ? "constant" : "global",
		    llty(G, t), buf);
		free(buf);
	}
}

static void gen_main_wrapper(CG *G)
{
	FnDecl *m = G->S->main_fn;
	bool args = m->params.len == 2;
	bool ret_int = m->ret_type->kind != TY_VOID;
	/* (WASI's start calls the C main(argc, argv) by its own name) */
	out(G->funcs, "define i32 @%s(i32 %%argc, ptr %%argv)%s {\n", codegen_wasm ? "__main_argc_argv" : "main",
	    checked_attrs());
	out(G->funcs, "\tcall void @cby_rt_init()\n");
	if (args) {
		out(G->funcs, "\t%%args = call ptr @cby_args(i32 %%argc, ptr %%argv)\n");
		out(G->funcs, "\t%%slice = alloca %%slice\n");
		out(G->funcs, "\t%%sd = getelementptr %%slice, ptr %%slice, i32 0, i32 0\n\tstore ptr %%args, ptr %%sd\n");
		out(G->funcs, "\t%%sl = getelementptr %%slice, ptr %%slice, i32 0, i32 1\n");
		out(G->funcs, "\t%%n = sext i32 %%argc to i64\n\tstore i64 %%n, ptr %%sl\n");
	}
	const char *call_args = args ? "i32 %argc, ptr %slice" : "";
	if (ret_int)
		out(G->funcs, "\t%%r = call i32 %s(%s)\n", fn_name(G, m), call_args);
	else
		out(G->funcs, "\tcall void %s(%s)\n", fn_name(G, m), call_args);
	if (args)
		out(G->funcs, "\tcall void @cby_args_free(ptr %%args)\n");
	out(G->funcs, "\t%%s = call i32 @cby_rt_finish(i32 %s)\n\tret i32 %%s\n}\n", ret_int ? "%r" : "0");
}

static const char *runtime_decls =
	"declare void @rt_print_int(i64)\n"
	"declare void @rt_print_uint(i64)\n"
	"declare void @rt_print_double(double)\n"
	"declare void @rt_print_float(float)\n"
	"declare void @rt_print_bool(i1 zeroext)\n"
	"declare void @rt_print_char(i8 zeroext)\n"
	"declare void @rt_print_sep()\n"
	"declare void @rt_print_newline()\n"
	"declare void @cby_print_str(ptr)\n"
	"declare void @rt_check_div(i64, i64, i32, i1 zeroext, i1 zeroext, ptr, i32, i32)\n"
	"declare void @rt_check_shift(i64, i32, ptr, i32, i32)\n"
	"declare void @rt_bounds_check(i64, i64, ptr, i32, i32)\n"
	"declare void @rt_str_bounds(i64, i64, ptr, i32, i32)\n"
	"declare void @cby_str_concat_n(ptr, ptr, i32)\n"
	"declare void @cby_list_reserve(ptr, i64, i64)\n"
	"declare ptr @cby_list_push(ptr, i64)\n"
	"declare ptr @cby_list_insert_slot(ptr, i64, i64, ptr, i32, i32)\n"
	"declare ptr @cby_list_at(ptr, i64, i64, ptr, i32, i32)\n"
	"declare void @cby_list_close_gap(ptr, i64, i64)\n"
	"declare ptr @cby_list_take_last(ptr, i64, ptr, i32, i32)\n"
	"declare void @cby_list_free(ptr)\n"
	"declare void @cby_list_join(ptr, ptr, ptr)\n"
	"declare void @cby_list_sort(ptr, i32)\n"
	"declare void @cby_list_sort_by(ptr, i64, ptr)\n"
	"declare void @cby_list_reverse(ptr, i64)\n"
	"declare void @rt_print_list_open()\n"
	"declare void @rt_print_list_sep(i64)\n"
	"declare void @rt_print_list_close()\n"
	"declare void @rt_print_lookup_open()\n"
	"declare void @rt_print_lookup_colon()\n"
	"declare void @rt_print_lookup_close()\n"
	"declare i64 @cby_lookup_find(ptr, i32, i64, ptr)\n"
	"declare i64 @cby_lookup_at(ptr, i32, i64, ptr, ptr, i32, i32)\n"
	"declare i64 @cby_lookup_put(ptr, i32, i64, i64, ptr, ptr)\n"
	"declare void @cby_lookup_kill(ptr, i32, i64, i64)\n"
	"declare i64 @cby_lookup_next(ptr, i64)\n"
	"declare void @cby_lookup_clear(ptr, i32, i64)\n"
	"declare void @cby_lookup_free(ptr, i32, i64)\n"
	"declare void @cby_lookup_copy_keys(ptr, ptr, i32, i64, i64)\n"
	"declare void @cby_lookup_keys(ptr, ptr, i32, i64)\n"
	"declare void @cby_lookup_print_key(ptr, i32, i64, i64)\n";

/* (in two parts: C promises string literals only up to 4095 characters) */
static const char *runtime_decls_files =
	"declare void @cby_files_read(ptr, ptr, ptr, i32, i32)\n"
	"declare void @cby_files_lines(ptr, ptr, ptr, i32, i32)\n"
	"declare void @cby_files_write(ptr, ptr, i1 zeroext, ptr, i32, i32)\n"
	"declare zeroext i1 @cby_files_exists(ptr)\n"
	"declare i64 @cby_files_size(ptr, ptr, i32, i32)\n"
	"declare void @cby_files_copy(ptr, ptr, ptr, i32, i32)\n"
	"declare void @cby_files_rename(ptr, ptr, ptr, i32, i32)\n"
	"declare void @cby_files_delete(ptr, ptr, i32, i32)\n"
	"declare void @cby_dirs_list(ptr, ptr, i1 zeroext, ptr, i32, i32)\n"
	"declare zeroext i1 @cby_dirs_exists(ptr)\n"
	"declare void @cby_dirs_make(ptr, ptr, i32, i32)\n"
	"declare void @cby_dirs_remove(ptr, ptr, i32, i32)\n"
	"declare void @cby_dirs_rename(ptr, ptr, ptr, i32, i32)\n"
	"declare void @cby_dirs_current(ptr)\n"
	"declare void @cby_dirs_change_to(ptr, ptr, i32, i32)\n"
	"declare void @cby_dirs_home(ptr)\n"
	"declare void @cby_dirs_save_folder(ptr, ptr, ptr, i32, i32)\n"
	"declare void @cby_events_run()\n"
	"declare ptr @cby_allocate(i64, i64, ptr, i32, i32)\n"
	"declare ptr @cby_reallocate(ptr, i64, i64, ptr, i32, i32)\n"
	"declare ptr @cby_text_address(ptr)\n"
	"declare void @cby_drop_nested(ptr, i64, ptr)\n"
	"declare void @rt_set_lib_call(ptr, i32, i32)\n"
	"declare void @rt_worker_check(ptr, ptr, i32, i32)\n"
	"declare void @free(ptr)\n"
	"declare void @cby_events_stop()\n"
	"declare void @cby_shell_run(ptr, ptr, ptr, i32, i32)\n"
	"declare void @cby_shell_get(ptr, ptr, ptr, i32, i32)\n"
	"declare i32 @cby_shell_status(ptr, ptr, i32, i32)\n"
	"declare zeroext i1 @cby_shell_has(ptr)\n"
	"declare void @cby_shell_set(ptr, ptr, ptr, i32, i32)\n"
	"declare void @cby_shell_unset(ptr, ptr, i32, i32)\n"
	"declare void @cby_files_open(ptr, ptr, i32, ptr, i32, i32)\n"
	"declare void @cby_file_drop(ptr)\n"
	"declare void @cby_file_read_line(ptr, ptr, ptr, i32, i32)\n"
	"declare zeroext i1 @cby_file_at_end(ptr, ptr, i32, i32)\n"
	"declare void @cby_out_push_file(ptr, ptr, i32, i32)\n";

static const char *runtime_decls_rest =
	"declare void @cby_str_split(ptr, ptr, ptr)\n"
	"declare void @rt_list_bounds(i64, i64, i1 zeroext, ptr, i32, i32)\n"
	"declare void @rt_list_slice(i64, i64, i64, i32, ptr, ptr, ptr, i32, i32)\n"
	"declare ptr @rt_alloc_zeroed(i64)\n"
	"declare ptr @rt_alloc(i64)\n"
	"declare void @rt_free(ptr)\n"
	"declare void @rt_out_of_memory()\n"
	"declare void @rt_obj_unlink(ptr)\n"
	"declare i64 @cby_link_obj(ptr)\n"
	"declare ptr @cby_link_get(i64, ptr, ptr, ptr, i32, i32)\n"
	"declare i1 @cby_link_is_set(i64)\n"
	"declare void @cby_rt_init()\n"
	"declare i32 @cby_rt_finish(i32)\n"
	"declare void @cby_str_dup(ptr, ptr, i64)\n"
	"declare void @cby_str_drop(ptr)\n"
	"declare zeroext i1 @cby_str_eq(ptr, ptr)\n"
	"declare ptr @cby_str_cstr(ptr)\n"
	"declare i64 @cby_str_len(ptr)\n"
	"declare void @cby_text(ptr, i32, i64, double)\n"
	"declare void @cby_read_line(ptr, ptr, i32, i32)\n"
	"declare void @cby_read_key(ptr, ptr, i32, i32)\n"
	"declare i8 @cby_read_char(ptr, i32, i32)\n"
	"declare void @rt_out_push(i32)\n"
	"declare ptr @cby_try_begin()\n"
	"@cby_trying = external thread_local global i32\n"
	"@cby_frame_top = external thread_local global ptr\n"
	"declare void @cby_frame_enter(ptr)\n"
	"declare i32 @_setjmp(ptr) returns_twice\n"
	"declare void @cby_try_end(ptr)\n"
	"declare void @cby_try_failed(ptr)\n"
	"declare void @cby_recovered(ptr)\n"
	"declare double @rt_random_double(double, double)\n"
	"declare i32 @rt_close_to(double, double, double, i32)\n"
	"declare i64 @rt_random_int(i64, i64)\n"
	"declare void @rt_random_seed(i64)\n"
	"declare void @cby_exit(i32) noreturn\n"
	"declare void @cby_text_decimals(ptr, double, i64)\n"
	"declare void @cby_text_decimals_int(ptr, i64, i1, i64)\n"
	"declare i64 @cby_str_find(ptr, ptr)\n"
	"declare i32 @cby_str_compare(ptr, ptr)\n"
	"declare zeroext i1 @cby_str_test(ptr, ptr, i32)\n"
	"declare void @cby_str_trim(ptr, ptr)\n"
	"declare void @cby_str_case(ptr, ptr, i1 zeroext)\n"
	"declare void @cby_str_replace(ptr, ptr, ptr, ptr)\n"
	"declare void @cby_str_repeat(ptr, ptr, i64, ptr, i32, i32)\n"
	"declare void @cby_str_pad(ptr, ptr, i64, i32)\n"
	"declare void @cby_str_slice(ptr, ptr, i64, i64, i32, ptr, i32, i32)\n"
	"declare void @rt_out_pop()\n"
	"declare void @cby_input(ptr, ptr, ptr, i32, i32)\n"
	"declare zeroext i1 @cby_end_of_input()\n"
	"declare zeroext i1 @cby_str_is_number(ptr, i32)\n"
	"declare i64 @cby_str_to_int(ptr, i32, ptr, i32, i32)\n"
	"declare double @cby_str_to_double(ptr, ptr, i32, i32)\n"
	"declare void @cby_str_copy(ptr, ptr)\n"
	"declare void @cby_str_concat(ptr, ptr, ptr)\n"
	"declare void @cby_str_append(ptr, ptr)\n"
	"declare signext i8 @cby_str_get(ptr, i64, ptr, i32, i32)\n"
	"declare void @cby_str_set(ptr, i64, i8 signext, ptr, i32, i32)\n"
	"declare ptr @cby_args(i32, ptr)\n"
	"declare void @cby_args_free(ptr)\n";

/* ---- list helpers, one set per element type ----
 * A loop over elements [from, from + count) of the list at `l`, calling
 * body(G, elem, pointer to the element, index, arg). */
static void list_elem_loop(CG *G, Type *el, Op data, Op from, Op count,
			   void (*body)(CG *, Type *, Op, Op, void *), void *arg)
{
	Op idx = entry_alloca(G, "i64", "i");
	Op head = lbl(G, "l.head"), loop = lbl(G, "l.body"), done = lbl(G, "l.done");
	Op end = tmp(G);
	emit(G, "%s = add i64 %s, %s", end.s, from.s, count.s);
	emit(G, "store i64 %s, ptr %s", from.s, idx.s);
	br(G, head);
	place_label(G, head);
	Op i = tmp(G), c = tmp(G);
	emit(G, "%s = load i64, ptr %s", i.s, idx.s);
	emit(G, "%s = icmp slt i64 %s, %s", c.s, i.s, end.s);
	emit_term(G, "br i1 %s, label %%%s, label %%%s", c.s, loop.s, done.s);
	place_label(G, loop);
	Op p = tmp(G);
	emit(G, "%s = getelementptr %s, ptr %s, i64 %s", p.s, llty(G, el), data.s, i.s);
	body(G, el, p, i, arg);
	Op n = tmp(G);
	emit(G, "%s = add i64 %s, 1", n.s, i.s);
	emit(G, "store i64 %s, ptr %s", n.s, idx.s);
	br(G, head);
	place_label(G, done);
}

static Op list_field(CG *G, Op l, int which)
{
	Op p = tmp(G), v = tmp(G);
	emit(G, "%s = getelementptr %%list, ptr %s, i32 0, i32 %d", p.s, l.s, which);
	emit(G, "%s = load %s, ptr %s", v.s, which ? "i64" : "ptr", p.s);
	return v;
}

static void drop_elem(CG *G, Type *el, Op p, Op i, void *arg) { (void)i; (void)arg; emit_drop(G, el, p); }

typedef struct { Op dst_data, from; } CopyArg;

/* A copy of the value at p, made at d */
static void copy_value(CG *G, Type *el, Op d, Op p)
{
	if (el->kind == TY_STR)
		emit(G, "call void @cby_str_copy(ptr %s, ptr %s)", d.s, p.s);
	else if (el->kind == TY_LIST)
		emit(G, "call void @\"lcopy.%d\"(ptr %s, ptr %s, i64 0, i64 -1)", list_id(G, el->inner), d.s, p.s);
	else if (el->kind == TY_LOOKUP)
		emit(G, "call void @\"kcopy.%d\"(ptr %s, ptr %s)", lookup_id(G, el), d.s, p.s);
	else
		memcpy_(G, d, p, size_of(el));
}

static void copy_elem(CG *G, Type *el, Op p, Op i, void *arg)
{
	CopyArg *ca = arg;
	Op k = tmp(G), d = tmp(G);
	emit(G, "%s = sub i64 %s, %s", k.s, i.s, ca->from.s);
	emit(G, "%s = getelementptr %s, ptr %s, i64 %s", d.s, llty(G, el), ca->dst_data.s, k.s);
	copy_value(G, el, d, p);
}

typedef struct { Op want; } FindArg;

static void find_elem(CG *G, Type *el, Op p, Op i, void *arg)
{
	FindArg *fa = arg;
	Op eq = tmp(G);
	if (el->kind == TY_STR) {
		emit(G, "%s = call i1 @cby_str_eq(ptr %s, ptr %s)", eq.s, p.s, fa->want.s);
	} else {
		Op a = load(G, el, p, false), b = load(G, el, fa->want, false);
		if (el->kind == TY_FLOAT)
			emit(G, "%s = fcmp oeq %s %s, %s", eq.s, vty(G, el), a.s, b.s);
		else
			emit(G, "%s = icmp eq %s %s, %s", eq.s, vty(G, el), a.s, b.s);
	}
	Op yes = lbl(G, "l.found"), no = lbl(G, "l.next");
	emit_term(G, "br i1 %s, label %%%s, label %%%s", eq.s, yes.s, no.s);
	place_label(G, yes);
	emit_term(G, "ret i64 %s", i.s);
	place_label(G, no);
}

static bool elem_copyable(const Type *t)
{
	if (type_is_copy(t) || t->kind == TY_STR)
		return true;
	return (t->kind == TY_LIST || t->kind == TY_LOOKUP) && elem_copyable(t->inner);
}

static bool elem_printable(const Type *t)
{
	if (type_is_arith(t) || t->kind == TY_STR)
		return true;
	return (t->kind == TY_LIST || t->kind == TY_LOOKUP) && elem_printable(t->inner);
}

static void print_elem(CG *G, Type *el, Op p, Op i, void *arg)
{
	(void)arg;
	emit(G, "call void @rt_print_list_sep(i64 %s)", i.s);
	print_one(G, el, el->kind == TY_STR || el->kind == TY_LIST || el->kind == TY_LOOKUP ? p : load(G, el, p, false));
}

static void gen_list_helper(CG *G, int id)
{
	{
		Type *el = G->lists.items[id];
		G->fn = NULL;

		/* ldrop.N: every element, then the storage */
		begin_function(G);
		if (type_needs_drop(el))
			list_elem_loop(G, el, list_field(G, op("%%l"), 0), op("0"), list_field(G, op("%%l"), 1), drop_elem, NULL);
		emit(G, "call void @cby_list_free(ptr %%l)");
		emit_term(G, "ret void");
		end_function(G, astr(G, "define internal void @\"ldrop.%d\"(ptr %%l)", id));

		/* lclear.N: every element; the storage is kept */
		begin_function(G);
		if (type_needs_drop(el))
			list_elem_loop(G, el, list_field(G, op("%%l"), 0), op("0"), list_field(G, op("%%l"), 1), drop_elem, NULL);
		Op lp = tmp(G);
		emit(G, "%s = getelementptr %%list, ptr %%l, i32 0, i32 1", lp.s);
		emit(G, "store i64 0, ptr %s", lp.s);
		emit_term(G, "ret void");
		end_function(G, astr(G, "define internal void @\"lclear.%d\"(ptr %%l)", id));

		/* lcopy.N(dst, src, from, count): a new list of copies; count -1: all */
		if (elem_copyable(el)) {
			begin_function(G);
			Op n = tmp(G), all = tmp(G);
			Op len = list_field(G, op("%%src"), 1);
			emit(G, "%s = icmp slt i64 %%count, 0", all.s);
			emit(G, "%s = select i1 %s, i64 %s, i64 %%count", n.s, all.s, len.s);
			memset0(G, op("%%dst"), 24);
			emit(G, "call void @cby_list_reserve(ptr %%dst, i64 %s, i64 %s)", elem_size(G, el), n.s);
			CopyArg ca = { list_field(G, op("%%dst"), 0), op("%%from") };
			list_elem_loop(G, el, list_field(G, op("%%src"), 0), op("%%from"), n, copy_elem, &ca);
			Op dl = tmp(G);
			emit(G, "%s = getelementptr %%list, ptr %%dst, i32 0, i32 1", dl.s);
			emit(G, "store i64 %s, ptr %s", n.s, dl.s);
			emit_term(G, "ret void");
			end_function(G, astr(G, "define internal void @\"lcopy.%d\"(ptr %%dst, ptr %%src, i64 %%from, i64 %%count)", id));
		}

		/* lprint.N(list): [a, b, c] */
		if (elem_printable(el)) {
			begin_function(G);
			emit(G, "call void @rt_print_list_open()");
			list_elem_loop(G, el, list_field(G, op("%%l"), 0), op("0"), list_field(G, op("%%l"), 1), print_elem, NULL);
			emit(G, "call void @rt_print_list_close()");
			emit_term(G, "ret void");
			end_function(G, astr(G, "define internal void @\"lprint.%d\"(ptr %%l)", id));
		}

		/* lfind.N(list, wanted): the first position holding it, or -1 */
		if (type_is_arith(el) || el->kind == TY_STR) {
			begin_function(G);
			FindArg fa = { op("%%want") };
			list_elem_loop(G, el, list_field(G, op("%%l"), 0), op("0"), list_field(G, op("%%l"), 1), find_elem, &fa);
			emit_term(G, "ret i64 -1");
			end_function(G, astr(G, "define internal i64 @\"lfind.%d\"(ptr %%l, ptr %%want)", id));
		}
	}
}

/* ---- L.sort_by and L.sort_with: sbefore.N(b, a), true when the element
 * at b goes before the one at a; cby_list_sort_by merge-sorts with it ---- */

/* the field (owner, index) of the element at p: a struct's in place, an
 * object's through its handle (and its class's offsets, if it has any
 * derived ones) */
static Op elem_field(CG *G, Type *el, Op p, int owner, int index)
{
	ObjectInfo *o = el->obj;
	int slot = o->block_start[owner] + index;
	Op r = tmp(G);
	if (o->kind == OBJ_STRUCT) {
		emit(G, "%s = getelementptr %%S.%d, ptr %s, i32 0, i32 %d", r.s, o->id, p.s, slot);
		return r;
	}
	Op h = tmp(G);
	emit(G, "%s = load ptr, ptr %s", h.s, p.s);
	if (!o->has_derived)
		return obj_field_static(G, o, h, slot);
	Op ci = tmp(G), offs = tmp(G), op_ = tmp(G), off = tmp(G), blk = tmp(G), slotp = tmp(G);
	emit(G, "%s = load ptr, ptr %s", ci.s, h.s);
	emit(G, "%s = getelementptr %%classinfo, ptr %s, i32 0, i32 1", offs.s, ci.s);
	emit(G, "%s = load ptr, ptr %s", op_.s, offs.s);
	emit(G, "%s = getelementptr i64, ptr %s, i64 %d", slotp.s, op_.s, owner);
	emit(G, "%s = load i64, ptr %s", off.s, slotp.s);
	emit(G, "%s = getelementptr i8, ptr %s, i64 %s", blk.s, h.s, off.s);
	emit(G, "%s = getelementptr %%B.%d, ptr %s, i32 0, i32 %d", r.s, owner, blk.s, index);
	return r;
}

/* the key of the element at p: where it is (a field), or a new value (a
 * function's; a str is put in *made, to be dropped) */
static Op sort_key(CG *G, Expr *cb, Type *el, Op p, Op *made)
{
	Type *k = cb->as.callback.key;
	FnDecl *fn = cb->as.callback.fn;
	if (!fn) {
		Op f = elem_field(G, el, p, cb->as.callback.owner_id, cb->as.callback.field_index);
		return k->kind == TY_STR ? f : load(G, k, f, false);
	}
	if (fn->definition)
		fn = fn->definition;
	if (k->kind == TY_STR) {
		*made = entry_alloca(G, llty(G, k), "key");
		emit(G, "call void %s(ptr %s, ptr %s)", fn_name(G, fn), made->s, p.s);
		return *made;
	}
	Op r = tmp(G);
	emit(G, "%s = call %s %s(ptr %s)", r.s, vty(G, k), fn_name(G, fn), p.s);
	return r;
}

static void gen_sort_helper(CG *G, int id)
{
	Expr *e = G->sorts.items[id];
	Expr *cb = e->as.call.args.items[1];
	Type *el = e->as.call.args.items[0]->ty->inner;
	G->fn = NULL;
	begin_function(G);
	Op r = tmp(G);
	if (e->as.call.builtin == BUILTIN_LIST_SORT_WITH) {
		FnDecl *fn = cb->as.callback.fn->definition ? cb->as.callback.fn->definition : cb->as.callback.fn;
		emit(G, "%s = call i1 %s(ptr %%b, ptr %%a)", r.s, fn_name(G, fn));
	} else {
		Type *k = cb->as.callback.key;
		Op mb = { "" }, ma = { "" };
		Op kb = sort_key(G, cb, el, op("%%b"), &mb), ka = sort_key(G, cb, el, op("%%a"), &ma);
		if (k->kind == TY_STR) {
			Op c = tmp(G);
			emit(G, "%s = call i32 @cby_str_compare(ptr %s, ptr %s)", c.s, kb.s, ka.s);
			emit(G, "%s = icmp slt i32 %s, 0", r.s, c.s);
			if (mb.s[0])
				emit_drop(G, k, mb);
			if (ma.s[0])
				emit_drop(G, k, ma);
		} else if (k->kind == TY_FLOAT) {
			/* (a float that isn't a number goes last, as .sort has it) */
			Op lt = tmp(G), na = tmp(G), nb = tmp(G), nan = tmp(G);
			const char *ft = vty(G, k);
			emit(G, "%s = fcmp olt %s %s, %s", lt.s, ft, kb.s, ka.s);
			emit(G, "%s = fcmp uno %s %s, %s", na.s, ft, ka.s, ka.s);
			emit(G, "%s = fcmp ord %s %s, %s", nb.s, ft, kb.s, kb.s);
			emit(G, "%s = and i1 %s, %s", nan.s, na.s, nb.s);
			emit(G, "%s = or i1 %s, %s", r.s, lt.s, nan.s);
		} else {
			bool uns = k->kind == TY_BOOL || k->is_char || !k->is_signed;
			emit(G, "%s = icmp %s %s %s, %s", r.s, uns ? "ult" : "slt", vty(G, k), kb.s, ka.s);
		}
	}
	emit_term(G, "ret i1 %s", r.s);
	end_function(G, astr(G, "define internal i1 @\"sbefore.%d\"(ptr %%b, ptr %%a)", id));
}

/* Calls body(G, lt, l, pos, arg) for each position in use, in order. */
static void lk_loop(CG *G, Type *lt, Op l, void (*body)(CG *, Type *, Op, Op, void *), void *arg)
{
	Op idx = entry_alloca(G, "i64", "k");
	Op head = lbl(G, "k.head"), loop = lbl(G, "k.body"), done = lbl(G, "k.done");
	Op first = tmp(G);
	emit(G, "%s = call i64 @cby_lookup_next(ptr %s, i64 0)", first.s, l.s);
	emit(G, "store i64 %s, ptr %s", first.s, idx.s);
	br(G, head);
	place_label(G, head);
	Op i = tmp(G), c = tmp(G);
	emit(G, "%s = load i64, ptr %s", i.s, idx.s);
	emit(G, "%s = icmp sge i64 %s, 0", c.s, i.s);
	emit_term(G, "br i1 %s, label %%%s, label %%%s", c.s, loop.s, done.s);
	place_label(G, loop);
	body(G, lt, l, i, arg);
	Op n = tmp(G), nx = tmp(G);
	emit(G, "%s = add i64 %s, 1", n.s, i.s);
	emit(G, "%s = call i64 @cby_lookup_next(ptr %s, i64 %s)", nx.s, l.s, n.s);
	emit(G, "store i64 %s, ptr %s", nx.s, idx.s);
	br(G, head);
	place_label(G, done);
}

static void lk_drop_value(CG *G, Type *lt, Op l, Op pos, void *arg)
{
	(void)arg;
	emit_drop(G, lt->inner, lk_at(G, lt, l, pos, 2));
}

/* copying values to the positions 0, 1, 2 ... of dst's storage */
typedef struct { Op data, count; } LkCopy;

static void lk_copy_value(CG *G, Type *lt, Op l, Op pos, void *arg)
{
	LkCopy *c = arg;
	Op j = tmp(G), d = tmp(G), j1 = tmp(G);
	emit(G, "%s = load i64, ptr %s", j.s, c->count.s);
	emit(G, "%s = getelementptr %s, ptr %s, i64 %s", d.s, llty(G, lt->inner), c->data.s, j.s);
	copy_value(G, lt->inner, d, lk_at(G, lt, l, pos, 2));
	emit(G, "%s = add i64 %s, 1", j1.s, j.s);
	emit(G, "store i64 %s, ptr %s", j1.s, c->count.s);
}

static void lk_print_entry(CG *G, Type *lt, Op l, Op pos, void *arg)
{
	Op count = *(Op *)arg, j = tmp(G), j1 = tmp(G);
	emit(G, "%s = load i64, ptr %s", j.s, count.s);
	emit(G, "call void @rt_print_list_sep(i64 %s)", j.s);
	emit(G, "%s = add i64 %s, 1", j1.s, j.s);
	emit(G, "store i64 %s, ptr %s", j1.s, count.s);
	emit(G, "call void @cby_lookup_print_key(ptr %s, i32 %d, i64 %s, i64 %s)", l.s, key_kind(lt->key),
	     elem_size(G, lt->key), pos.s);
	emit(G, "call void @rt_print_lookup_colon()");
	Type *v = lt->inner;
	Op p = lk_at(G, lt, l, pos, 2);
	print_one(G, v, v->kind == TY_STR || v->kind == TY_LIST || v->kind == TY_LOOKUP ? p : load(G, v, p, false));
}

static void gen_lookup_helper(CG *G, int id)
{
	Type *lt = G->lookups.items[id];
	int kk = key_kind(lt->key);
	const char *ks = elem_size(G, lt->key), *vs = elem_size(G, lt->inner);
	G->fn = NULL;

	/* kdrop.N: every value, then the keys and storage */
	begin_function(G);
	if (type_needs_drop(lt->inner))
		lk_loop(G, lt, op("%%l"), lk_drop_value, NULL);
	emit(G, "call void @cby_lookup_free(ptr %%l, i32 %d, i64 %s)", kk, ks);
	emit_term(G, "ret void");
	end_function(G, astr(G, "define internal void @\"kdrop.%d\"(ptr %%l)", id));

	/* kclear.N: every value, then the keys; the storage is kept */
	begin_function(G);
	if (type_needs_drop(lt->inner))
		lk_loop(G, lt, op("%%l"), lk_drop_value, NULL);
	emit(G, "call void @cby_lookup_clear(ptr %%l, i32 %d, i64 %s)", kk, ks);
	emit_term(G, "ret void");
	end_function(G, astr(G, "define internal void @\"kclear.%d\"(ptr %%l)", id));

	if (elem_copyable(lt->inner)) {
		/* kcopy.N(dst, src): the keys in order, then a copy of each value */
		begin_function(G);
		emit(G, "call void @cby_lookup_copy_keys(ptr %%dst, ptr %%src, i32 %d, i64 %s, i64 %s)", kk, ks, vs);
		LkCopy c = { lk_field(G, op("%%dst"), 1), entry_alloca(G, "i64", "j") };
		emit(G, "store i64 0, ptr %s", c.count.s);
		lk_loop(G, lt, op("%%src"), lk_copy_value, &c);
		emit_term(G, "ret void");
		end_function(G, astr(G, "define internal void @\"kcopy.%d\"(ptr %%dst, ptr %%src)", id));

		/* kvalues.N(dst, src): a list of copies of the values, in order */
		begin_function(G);
		memset0(G, op("%%dst"), 24);
		Op n = lk_field(G, op("%%src"), 5);
		emit(G, "call void @cby_list_reserve(ptr %%dst, i64 %s, i64 %s)", vs, n.s);
		LkCopy v = { list_field(G, op("%%dst"), 0), entry_alloca(G, "i64", "j") };
		emit(G, "store i64 0, ptr %s", v.count.s);
		lk_loop(G, lt, op("%%src"), lk_copy_value, &v);
		Op lp = tmp(G);
		emit(G, "%s = getelementptr %%list, ptr %%dst, i32 0, i32 1", lp.s);
		emit(G, "store i64 %s, ptr %s", n.s, lp.s);
		emit_term(G, "ret void");
		end_function(G, astr(G, "define internal void @\"kvalues.%d\"(ptr %%dst, ptr %%src)", id));
	}

	if (elem_printable(lt->inner)) {
		/* kprint.N: {key: value, ...} */
		begin_function(G);
		emit(G, "call void @rt_print_lookup_open()");
		Op count = entry_alloca(G, "i64", "j");
		emit(G, "store i64 0, ptr %s", count.s);
		lk_loop(G, lt, op("%%l"), lk_print_entry, &count);
		emit(G, "call void @rt_print_lookup_close()");
		emit_term(G, "ret void");
		end_function(G, astr(G, "define internal void @\"kprint.%d\"(ptr %%l)", id));
	}
}

void codegen_emit(Sema *S, FILE *o)
{
	CG G;
	memset(&G, 0, sizeof G);
	G.S = S;
	arena_init(&G.A);
	G.types = open_memstream(&G.types_buf, &G.types_len);
	G.glob = open_memstream(&G.glob_buf, &G.glob_len);
	G.decl = open_memstream(&G.decl_buf, &G.decl_len);
	G.funcs = open_memstream(&G.func_buf, &G.func_len);

	gen_type_defs(&G);
	gen_classinfo(&G);
	gen_globals(&G);
	for (int i = 0; i < S->nobjects; i++)
		if (S->objects[i]->kind == OBJ_OBJECT)
			gen_object_support(&G, S->objects[i]);
	for (int i = 0; i < S->functions.len; i++)
		gen_function(&G, S->functions.items[i]);
	gen_main_wrapper(&G);
	for (int i = 0; i < G.fdrops.len; i++)
		gen_fdrop_helper(&G, i);
	for (int i = 0; i < G.sorts.len; i++)
		gen_sort_helper(&G, i);
	/* helpers can need more helpers (a list of lookups, say): until none are new */
	for (int li = 0, ki = 0; li < G.lists.len || ki < G.lookups.len;) {
		while (li < G.lists.len)
			gen_list_helper(&G, li++);
		while (ki < G.lookups.len)
			gen_lookup_helper(&G, ki++);
	}

	fclose(G.types);
	fclose(G.glob);
	fclose(G.decl);
	fclose(G.funcs);
	fprintf(o, "; generated by cabby from %s\n\n", S->prog->main_file);
	fprintf(o, "%s\n%s\n%s%s%s%s%s\n%s\n!0 = !{!\"branch_weights\", i32 1, i32 2000}\n", G.types_buf, G.glob_buf,
		runtime_decls, runtime_decls_files, runtime_decls_rest,
		codegen_wasm ? "declare i32 @setjmp(ptr) returns_twice\n" : "", G.decl_buf, G.func_buf);
	free(G.types_buf);
	free(G.glob_buf);
	free(G.decl_buf);
	free(G.func_buf);
	arena_free(&G.A);
}

/* ============================================================== building */

static int run(char *const argv[])
{
	pid_t pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		execvp(argv[0], argv);
		_exit(127);
	}
	int status;
	if (waitpid(pid, &status, 0) < 0)
		return -1;
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static const char *find_runtime(const char *const *dirs, char *buf, size_t size)
{
	for (const char *const *d = dirs; d && *d; d++) {
		snprintf(buf, size, "%s/libcabbyrt.a", *d);
		struct stat st;
		if (stat(buf, &st) == 0)
			return buf;
	}
	return NULL;
}
/* The IR Cabby writes uses LLVM's plain `ptr` type, which clang reads from
 * version 15 (Apple's clang from 14.0.3, which is LLVM 15). An older one
 * fails with a puzzling message about types, so say so plainly first. */
static bool clang_new_enough(const char *cc)
{
	char cmd[512], line[256] = "";
	snprintf(cmd, sizeof cmd, "'%s' --version 2>/dev/null", cc);
	FILE *p = popen(cmd, "r");
	if (!p)
		return true;
	if (!fgets(line, sizeof line, p))
		line[0] = '\0';
	pclose(p);
	const char *v = strstr(line, "clang version ");
	if (!v)
		return true;   /* (not a clang we know how to read: let it try) */
	int major = 0, minor = 0, patch = 0;
	sscanf(v + 14, "%d.%d.%d", &major, &minor, &patch);
	bool apple = strncmp(line, "Apple", 5) == 0;
	bool ok = apple ? major > 14 || (major == 14 && minor > 0) || (major == 14 && patch >= 3) : major >= 15;
	if (!ok) {
		line[strcspn(line, "\n")] = '\0';
		fprintf(stderr, "cabby: compiling needs clang 15 or newer (on a Mac, Apple clang 14.0.3 or newer), and '%s' is\n"
				"  %s\n"
				"  Update it (on a Mac: Software Update, for the Command Line Tools), or set CABBY_CC to a newer clang.\n",
			cc, line);
	}
	return ok;
}

/* ---- for WebAssembly (--wasm): wasi-sdk's clang, and the runtime built
 * for it (lib/wasm/libcabbyrt.a, from make wasm) */

/* wasi-sdk: $CABBY_WASI_SDK, the one Cabby was built with, or one in
 * ~/tools or /opt */
static const char *find_wasi_sdk(char *buf, size_t size)
{
	const char *tries[4] = { getenv("CABBY_WASI_SDK"), NULL, NULL, "/opt/wasi-sdk" };
#ifdef CABBY_WASI_SDK
	tries[1] = CABBY_WASI_SDK;
#endif
	char home_tools[4096] = "";
	const char *home = getenv("HOME");
	if (home && *home) {
		/* (the newest of ~/tools/wasi-sdk-*) */
		snprintf(home_tools, sizeof home_tools, "%s/tools", home);
		DIR *d = opendir(home_tools);
		char best[256] = "";
		if (d) {
			struct dirent *e;
			while ((e = readdir(d)))
				if (strncmp(e->d_name, "wasi-sdk", 8) == 0 && strcmp(e->d_name, best) > 0)
					snprintf(best, sizeof best, "%s", e->d_name);
			closedir(d);
		}
		if (*best) {
			size_t n = strlen(home_tools);
			snprintf(home_tools + n, sizeof home_tools - n, "/%s", best);
			tries[2] = home_tools;
		}
	}
	for (int i = 0; i < 4; i++) {
		if (!tries[i] || !*tries[i])
			continue;
		snprintf(buf, size, "%s/bin/clang", tries[i]);
		if (access(buf, X_OK) == 0)
			return buf;
	}
	return NULL;
}

static int compile_wasm(Sema *S, const CompileOptions *opt, const char *out_name)
{
	char rt[4096] = "";
	for (const char *const *d = opt->lib_dirs; d && *d; d++) {
		snprintf(rt, sizeof rt, "%s/wasm/libcabbyrt.a", *d);
		if (access(rt, R_OK) == 0)
			break;
		rt[0] = '\0';
	}
	if (!*rt) {
		fprintf(stderr, "cabby: --wasm needs the runtime built for WebAssembly (lib/wasm/libcabbyrt.a): "
			"'make wasm' in the cabby folder, with wasi-sdk\n");
		return 1;
	}
	char cc[4096];
	if (!find_wasi_sdk(cc, sizeof cc)) {
		fprintf(stderr, "cabby: --wasm needs wasi-sdk (https://github.com/WebAssembly/wasi-sdk/releases), "
			"unpacked into ~/tools or /opt/wasi-sdk, or set CABBY_WASI_SDK to where it is\n");
		return 1;
	}
	char ll[] = "/tmp/cabby-XXXXXX.ll";
	int fd = mkstemps(ll, 3);
	if (fd < 0) {
		fprintf(stderr, "cabby: can't create a temporary file\n");
		return 1;
	}
	FILE *f = fdopen(fd, "w");
	codegen_checked = false;
	codegen_emit(S, f);
	fclose(f);
	char olevel[8];
	snprintf(olevel, sizeof olevel, "-O%d", opt->opt_level);
	/* each library's native code built for WebAssembly (lib/wasm/libcabby<name>.a,
	 * from make wasm: the game library's), and SDL, which draws for it */
	char *libs[40];
	int nlibs = 0;
	char sdl[4096] = "";
	Program *prog = S->prog;
	for (int i = 0; i < prog->natives.len && nlibs < 38; i++) {
		NativeLib *nl = &prog->natives.items[i];
		char path[4096];
		snprintf(path, sizeof path, "%s/wasm/libcabby%s.a", nl->dir, nl->name);
		if (access(path, R_OK) != 0)
			continue;
		libs[nlibs++] = strdup(path);
		snprintf(path, sizeof path, "%s/wasm/libSDL3.a", nl->dir);
		if (!*sdl && access(path, R_OK) == 0)
			snprintf(sdl, sizeof sdl, "%s", path);
	}
	if (*sdl)
		libs[nlibs++] = sdl;
	/* (setjmp as WebAssembly's exceptions, for panics caught by `else`; the
	 * C library's emulated parts the runtime uses; 8 MB of stack; and no
	 * names, which only a debugger reads: hello is 17 KB, not 420) */
	char *argv[80];
	int argc = 0;
	char *const head[] = { cc, "--target=wasm32-wasip1", olevel, "-Wno-override-module", "-mexception-handling",
			       "-mllvm", "-wasm-enable-sjlj", "-mllvm", "-wasm-use-legacy-eh=false", ll };
	for (size_t i = 0; i < sizeof head / sizeof *head; i++)
		argv[argc++] = head[i];
	for (int i = 0; i < nlibs; i++)
		argv[argc++] = libs[i];   /* (before the runtime, whose stand-ins they replace) */
	char *const tail[] = { rt, "-lsetjmp", "-lwasi-emulated-signal", "-lwasi-emulated-process-clocks",
			       "-lwasi-emulated-mman", "-lwasi-emulated-getpid", "-lm", "-Wl,-z,stack-size=8388608",
			       "-Wl,--max-memory=2147483648", "-Wl,--strip-all", "-o", (char *)out_name };
	for (size_t i = 0; i < sizeof tail / sizeof *tail; i++)
		argv[argc++] = tail[i];
	argv[argc] = NULL;
	int rc = run(argv);
	unlink(ll);
	if (rc != 0) {
		fprintf(stderr, "cabby: building WebAssembly with '%s' failed (see the messages above)\n", cc);
		return 1;
	}
	return 0;
}

int compile_program(Sema *S, const char *src_path, const CompileOptions *opt)
{
	char out_name[4096];
	if (opt->output) {
		snprintf(out_name, sizeof out_name, "%s", opt->output);
	} else {
		const char *base = strrchr(src_path, '/');
		base = base ? base + 1 : src_path;
		snprintf(out_name, sizeof out_name, "%s", base);
		char *dot = strrchr(out_name, '.');
		if (dot && strcmp(dot, ".cby") == 0)
			*dot = '\0';
		if (opt->emit_llvm)
			strncat(out_name, ".ll", sizeof out_name - strlen(out_name) - 1);
		else if (opt->wasm)
			strncat(out_name, ".wasm", sizeof out_name - strlen(out_name) - 1);
		else if (strcmp(out_name, base) == 0)
			strncat(out_name, ".out", sizeof out_name - strlen(out_name) - 1);
	}

	codegen_wasm = opt->wasm;
	if (opt->wasm && opt->checked) {
		fprintf(stderr, "cabby: --checked and --wasm can't go together (AddressSanitizer isn't there in WebAssembly)\n");
		return 1;
	}
	if (opt->wasm && !opt->emit_llvm)
		return compile_wasm(S, opt, out_name);
	if (opt->emit_llvm) {
		FILE *f = fopen(out_name, "w");
		if (!f) {
			fprintf(stderr, "cabby: can't write '%s'\n", out_name);
			return 1;
		}
		codegen_checked = opt->checked;
		codegen_emit(S, f);
		fclose(f);
		return 0;
	}

	char rt_buf[4096];
	const char *rt = find_runtime(opt->lib_dirs, rt_buf, sizeof rt_buf);
	if (!rt) {
		fprintf(stderr, "cabby: can't find the runtime library libcabbyrt.a (set CABBY_LIB to its directory)\n");
		return 1;
	}
	char ll[] = "/tmp/cabby-XXXXXX.ll";
	int fd = mkstemps(ll, 3);
	if (fd < 0) {
		fprintf(stderr, "cabby: can't create a temporary file\n");
		return 1;
	}
	FILE *f = fdopen(fd, "w");
	codegen_checked = opt->checked;
	codegen_emit(S, f);
	fclose(f);

	const char *cc = getenv("CABBY_CC");
	if (!cc || !*cc)
		cc = "clang";
	if (!clang_new_enough(cc))
		return 1;
	char olevel[8];
	/* (--checked: -O1 at most, or LLVM works out what an array holds before
	 * AddressSanitizer looks, and a read past its end isn't there to catch) */
	snprintf(olevel, sizeof olevel, "-O%d", opt->checked && opt->opt_level > 1 ? 1 : opt->opt_level);
	/* clang, the program, then each library header's native code
	 * (libcabby<name>.a and the flags in <name>.link), the runtime, libm */
	char *argv[256];
	int argc = 0;
	argv[argc++] = (char *)cc;
	argv[argc++] = olevel;
	argv[argc++] = "-Wno-override-module";
	if (opt->checked) {
		/* AddressSanitizer: reading past what's there, using memory after
		 * it's freed, and (on Linux) memory never freed, each said where */
		argv[argc++] = "-fsanitize=address";
		argv[argc++] = "-fno-omit-frame-pointer";
		argv[argc++] = "-g";
	}
	argv[argc++] = ll;
	Program *prog = S->prog;
	for (int i = 0; i < prog->natives.len && argc < 200; i++) {
		NativeLib *nl = &prog->natives.items[i];
		char path[4096];
		snprintf(path, sizeof path, "%s/libcabby%s.a", nl->dir, nl->name);
		if (access(path, R_OK) != 0)
			continue;
		argv[argc++] = strdup(path);
		snprintf(path, sizeof path, "%s/%s.link", nl->dir, nl->name);
		FILE *lf = fopen(path, "r");
		if (lf) {
			char word[4096];
			while (argc < 240 && fscanf(lf, "%4095s", word) == 1)
				argv[argc++] = strdup(word);
			fclose(lf);
		}
	}
	argv[argc++] = (char *)rt;
	argv[argc++] = "-lm";
	argv[argc++] = "-pthread";   /* (workers: use threads) */
	argv[argc++] = "-rdynamic";  /* (windows and games find the event loop by name: dlsym) */
	argv[argc++] = "-o";
	argv[argc++] = out_name;
	argv[argc] = NULL;
	int rc = run(argv);
	unlink(ll);
	if (rc == 127 || rc < 0) {
		fprintf(stderr, "cabby: could not run '%s' (install clang, or set CABBY_CC)\n", cc);
		return 1;
	}
	if (rc != 0) {
		fprintf(stderr, "cabby: building with '%s' failed (see the messages above)\n", cc);
		if (opt->checked)
			fprintf(stderr, "cabby: --checked needs clang's AddressSanitizer: on Debian, libclang-rt-dev "
				"(libclang-rt-19-dev for clang 19); on Arch, compiler-rt; a Mac's clang has it\n");
		return 1;
	}
	return 0;
}
