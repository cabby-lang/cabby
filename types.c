#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "sema.h"

Type t_error  = { .kind = TY_ERROR };
Type t_void   = { .kind = TY_VOID };
Type t_bool   = { .kind = TY_BOOL, .bits = 8 };
Type t_char   = { .kind = TY_INT, .bits = 8,  .is_signed = true,  .is_char = true };
Type t_schar  = { .kind = TY_INT, .bits = 8,  .is_signed = true,  .is_char = true, .name = "signed char" };
Type t_uchar  = { .kind = TY_INT, .bits = 8,  .is_signed = false, .is_char = true };
Type t_short  = { .kind = TY_INT, .bits = 16, .is_signed = true };
Type t_ushort = { .kind = TY_INT, .bits = 16, .is_signed = false };
Type t_int    = { .kind = TY_INT, .bits = 32, .is_signed = true };
Type t_uint   = { .kind = TY_INT, .bits = 32, .is_signed = false };
Type t_long   = { .kind = TY_INT, .bits = 64, .is_signed = true };
Type t_ulong  = { .kind = TY_INT, .bits = 64, .is_signed = false };
Type t_float  = { .kind = TY_FLOAT, .bits = 32 };
Type t_double = { .kind = TY_FLOAT, .bits = 64 };
Type t_handler = { .kind = TY_HANDLER };
Type t_file    = { .kind = TY_FILE, .methods = "file" };   /* files.hby adds methods: file.read_bytes */
Type t_str    = { .kind = TY_STR };

Type *type_ref(Arena *a, Type *inner, bool is_mut)
{
	Type *t = arena_alloc(a, sizeof *t);
	t->kind = TY_REF;
	t->inner = inner;
	t->is_mut = is_mut;
	return t;
}

Type *type_slice(Arena *a, Type *inner, bool is_mut)
{
	Type *t = arena_alloc(a, sizeof *t);
	t->kind = TY_SLICE;
	t->inner = inner;
	t->is_mut = is_mut;
	return t;
}

Type *type_array(Arena *a, Type *inner, uint64_t len)
{
	Type *t = arena_alloc(a, sizeof *t);
	t->kind = TY_ARRAY;
	t->inner = inner;
	t->len = len;
	return t;
}

Type *type_list(Arena *a, Type *inner)
{
	Type *t = arena_alloc(a, sizeof *t);
	t->kind = TY_LIST;
	t->inner = inner;
	return t;
}

Type *type_address(Arena *a, Type *inner)
{
	Type *t = arena_alloc(a, sizeof *t);
	t->kind = TY_ADDRESS;
	t->inner = inner;
	return t;
}

Type *type_link(Arena *a, Type *inner)
{
	Type *t = arena_alloc(a, sizeof *t);
	t->kind = TY_LINK;
	t->inner = inner;
	return t;
}

Type *type_lookup(Arena *a, Type *key, Type *value)
{
	Type *t = arena_alloc(a, sizeof *t);
	t->kind = TY_LOOKUP;
	t->key = key;
	t->inner = value;
	return t;
}

Type *type_int_named(Arena *a, const Type *base, const char *name)
{
	Type *t = arena_alloc(a, sizeof *t);
	*t = *base;
	t->name = name;
	return t;
}

bool type_is_error(const Type *t) { return !t || t->kind == TY_ERROR; }
bool type_is_int(const Type *t) { return t && t->kind == TY_INT; }
bool type_is_arith(const Type *t) { return t && (t->kind == TY_INT || t->kind == TY_FLOAT || t->kind == TY_BOOL); }

static bool same_kind(const Type *a, const Type *b)
{
	if (!a->kind_name || !b->kind_name)
		return !a->kind_name && !b->kind_name;
	return strcmp(a->kind_name, b->kind_name) == 0;
}

bool type_eq(const Type *a, const Type *b)
{
	if (a == b)
		return true;
	if (!a || !b || a->kind != b->kind)
		return false;
	switch (a->kind) {
	case TY_INT:    return a->bits == b->bits && a->is_signed == b->is_signed && a->is_char == b->is_char &&
			       same_kind(a, b);
	case TY_FLOAT:  return a->bits == b->bits && same_kind(a, b);
	case TY_REF:
	case TY_SLICE:  return a->is_mut == b->is_mut && type_eq(a->inner, b->inner);
	case TY_ARRAY:  return a->len == b->len && type_eq(a->inner, b->inner);
	case TY_LIST:   return type_eq(a->inner, b->inner);
	case TY_LINK:   return type_eq(a->inner, b->inner);
	case TY_ADDRESS: return (!a->inner && !b->inner) || (a->inner && b->inner && type_eq(a->inner, b->inner));
	case TY_LOOKUP: return type_eq(a->key, b->key) && type_eq(a->inner, b->inner);
	case TY_OBJECT: return a->obj == b->obj;
	case TY_CALLBACK: return a->sig == b->sig;
	default:        return true;
	}
}

bool type_is_plain(const Type *t)
{
	switch (t->kind) {
	case TY_BOOL: case TY_INT: case TY_FLOAT: case TY_ERROR: case TY_ADDRESS: case TY_CALLBACK: case TY_LINK:
		return true;
	case TY_ARRAY:
		return type_is_plain(t->inner);
	case TY_OBJECT:
		if (t->obj->kind == OBJ_OBJECT)
			return false;
		for (int i = 0; i < t->obj->field_count; i++)
			if (!type_is_plain(t->obj->fields[i].type))
				return false;
		return true;
	default:
		return false;
	}
}

bool type_is_copy(const Type *t)
{
	switch (t->kind) {
	case TY_REF:
	case TY_SLICE:
		return !t->is_mut;
	case TY_VOID:
	case TY_HANDLER:
		return true;
	default:
		return type_is_plain(t);
	}
}

bool type_needs_drop(const Type *t)
{
	switch (t->kind) {
	case TY_STR:
	case TY_LIST:
	case TY_LOOKUP:
	case TY_FILE:
		return true;
	case TY_ARRAY:
		return type_needs_drop(t->inner);
	case TY_OBJECT:
		if (t->obj->kind == OBJ_OBJECT)
			return true;
		for (int i = 0; i < t->obj->field_count; i++)
			if (type_needs_drop(t->obj->fields[i].type))
				return true;
		return false;
	default:
		return false;
	}
}

bool type_contains_ref(const Type *t)
{
	if (t->kind == TY_REF || t->kind == TY_SLICE || t->kind == TY_HANDLER)
		return true;
	if (t->kind == TY_ARRAY)
		return type_contains_ref(t->inner);
	return false;
}

Type *type_deref(Type *t) { return (t && t->kind == TY_REF) ? t->inner : t; }

bool object_is_subclass(const ObjectInfo *derived, const ObjectInfo *base)
{
	for (int i = 0; i < derived->mro_len; i++)
		if (derived->mro[i] == base)
			return true;
	return false;
}

int type_int_rank(const Type *t)
{
	if (t->kind == TY_BOOL)
		return 0;
	switch (t->bits) {
	case 8:  return 1;
	case 16: return 2;
	case 32: return 3;
	default: return 4;
	}
}

Type *type_promote(Type *t)
{
	if (t->kind == TY_BOOL)
		return &t_int;
	if (t->kind == TY_INT && t->bits < 32)
		return &t_int;   /* every 8/16-bit value fits in int */
	if (t->kind == TY_INT && t->name)   /* enums and named ints behave as their base */
		return t->bits == 32 ? (t->is_signed ? &t_int : &t_uint) : (t->is_signed ? &t_long : &t_ulong);
	return t;
}

static Type *canonical_int(int bits, bool is_signed)
{
	if (bits == 32)
		return is_signed ? &t_int : &t_uint;
	return is_signed ? &t_long : &t_ulong;
}

Type *type_common(Type *a, Type *b)
{
	if (a->kind == TY_FLOAT || b->kind == TY_FLOAT) {
		int bits = 0;
		if (a->kind == TY_FLOAT && a->bits > bits) bits = a->bits;
		if (b->kind == TY_FLOAT && b->bits > bits) bits = b->bits;
		return bits == 64 ? &t_double : &t_float;
	}
	a = type_promote(a);
	b = type_promote(b);
	if (a->is_signed == b->is_signed)
		return canonical_int(a->bits > b->bits ? a->bits : b->bits, a->is_signed);
	Type *u = a->is_signed ? b : a;
	Type *s = a->is_signed ? a : b;
	if (u->bits >= s->bits)
		return canonical_int(u->bits, false);
	return canonical_int(s->bits, true);   /* signed type holds every unsigned value */
}

bool int_fits(const Type *t, int64_t v, bool v_unsigned)
{
	if (t->kind == TY_BOOL)
		return !v_unsigned && (v == 0 || v == 1);
	if (t->kind != TY_INT)
		return false;
	if (v_unsigned) {
		uint64_t u = (uint64_t)v;
		if (t->is_signed)
			return t->bits == 64 ? u <= (uint64_t)INT64_MAX : u < ((uint64_t)1 << (t->bits - 1));
		return t->bits == 64 || u < ((uint64_t)1 << t->bits);
	}
	if (t->is_signed) {
		if (t->bits == 64)
			return true;
		int64_t lim = (int64_t)1 << (t->bits - 1);
		return v >= -lim && v < lim;
	}
	if (v < 0)
		return false;
	return t->bits == 64 || (uint64_t)v < ((uint64_t)1 << t->bits);
}

uint64_t type_align(const Type *t)
{
	switch (t->kind) {
	case TY_BOOL:  return 1;
	case TY_INT:
	case TY_FLOAT: return t->bits / 8;
	case TY_ARRAY: return type_align(t->inner);
	case TY_OBJECT:
		return t->obj->kind == OBJ_OBJECT ? 8 : (t->obj->align ? t->obj->align : 1);
	default:       return 8;
	}
}

uint64_t type_size(const Type *t)
{
	switch (t->kind) {
	case TY_BOOL:  return 1;
	case TY_INT:
	case TY_FLOAT: return t->bits / 8;
	case TY_STR:   return 16;
	case TY_LIST:  return 24;
	case TY_LOOKUP: return 64;
	case TY_FILE:  return 8;
	case TY_REF:   return 8;
	case TY_SLICE: return 16;
	case TY_HANDLER: return 16;
	case TY_ADDRESS: return 8;
	case TY_CALLBACK: return 8;
	case TY_LINK:  return 8;
	case TY_ARRAY: return t->len * type_size(t->inner);
	case TY_OBJECT:
		return t->obj->kind == OBJ_OBJECT ? 8 : t->obj->size;
	default:       return 0;
	}
}

static void put(char *buf, size_t size, size_t *n, const char *s)
{
	size_t len = strlen(s);
	if (*n + len >= size)
		len = size - *n - 1;
	memcpy(buf + *n, s, len);
	*n += len;
	buf[*n] = '\0';
}

static const char *int_name(const Type *t)
{
	if (t->name)
		return t->name;
	if (t->is_char)
		return t->is_signed ? "char" : "unsigned char";
	switch (t->bits) {
	case 16: return t->is_signed ? "short" : "unsigned short";
	case 32: return t->is_signed ? "int" : "unsigned int";
	default: return t->is_signed ? "long" : "unsigned long";
	}
}

static void fmt(const Type *t, char *buf, size_t size, size_t *n)
{
	char tmp[32];
	if (!t) {
		put(buf, size, n, "?");
		return;
	}
	switch (t->kind) {
	case TY_ERROR:  put(buf, size, n, "<error>"); break;
	case TY_VOID:   put(buf, size, n, "void"); break;
	case TY_BOOL:   put(buf, size, n, "bool"); break;
	case TY_INT:    put(buf, size, n, int_name(t)); break;
	case TY_FLOAT:  put(buf, size, n, t->bits == 32 ? "float" : "double"); break;
	case TY_STR:    put(buf, size, n, "str"); break;
	case TY_LIST:   put(buf, size, n, "list "); fmt(t->inner, buf, size, n); break;
	case TY_LINK:   put(buf, size, n, "link "); fmt(t->inner, buf, size, n); break;
	case TY_LOOKUP:
		put(buf, size, n, "lookup ");
		fmt(t->key, buf, size, n);
		put(buf, size, n, " ");
		fmt(t->inner, buf, size, n);
		break;
	case TY_HANDLER: put(buf, size, n, "handler"); break;
	case TY_CALLBACK: put(buf, size, n, t->name ? t->name : "callback"); break;
	case TY_ADDRESS:
		put(buf, size, n, "address");
		if (t->inner) {
			put(buf, size, n, " ");
			fmt(t->inner, buf, size, n);
		}
		break;
	case TY_FILE:   put(buf, size, n, "file"); break;
	case TY_OBJECT: put(buf, size, n, t->obj->name); break;
	case TY_REF:
		if (!t->is_mut)
			put(buf, size, n, "const ");
		fmt(t->inner, buf, size, n);
		put(buf, size, n, " &");
		break;
	case TY_SLICE:
		if (!t->is_mut)
			put(buf, size, n, "const ");
		fmt(t->inner, buf, size, n);
		put(buf, size, n, "[]");
		break;
	case TY_ARRAY: {
		/* C order: element type, then dimensions outermost first */
		const Type *e = t;
		while (e->kind == TY_ARRAY)
			e = e->inner;
		fmt(e, buf, size, n);
		for (const Type *d = t; d->kind == TY_ARRAY; d = d->inner) {
			snprintf(tmp, sizeof tmp, "[%" PRIu64 "]", d->len);
			put(buf, size, n, tmp);
		}
		break;
	}
	}
}

const char *type_str(const Type *t, char *buf, size_t size)
{
	size_t n = 0;
	buf[0] = '\0';
	fmt(t, buf, size, &n);
	return buf;
}
