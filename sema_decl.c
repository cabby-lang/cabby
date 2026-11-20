/* sema_decl.c — stages 1 and 2: declarations, types and the object model. */
#include "sema_int.h"

/* ====================================================== diagnostics */

static void vdiag(DiagLevel lvl, SrcLoc l, const char *fmt, va_list ap)
{
	char buf[700];
	vsnprintf(buf, sizeof buf, fmt, ap);
	diag_report_in(lvl, l.file, l.line, l.col, "%s", buf);
}

void sema_error(SrcLoc l, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vdiag(DIAG_ERROR, l, fmt, ap);
	va_end(ap);
}

void sema_note(SrcLoc l, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vdiag(DIAG_NOTE, l, fmt, ap);
	va_end(ap);
}

void sema_warning(SrcLoc l, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vdiag(DIAG_WARNING, l, fmt, ap);
	va_end(ap);
}

const char *ts(const Type *t)
{
	static char bufs[8][160];
	static int i;
	i = (i + 1) % 8;
	return type_str(t, bufs[i], sizeof bufs[i]);
}

Builtin find_builtin(const char *name)
{
	if (strcmp(name, "print") == 0)   return BUILTIN_PRINT;
	if (strcmp(name, "println") == 0) return BUILTIN_PRINTLN;
	if (strcmp(name, "read_line") == 0) return BUILTIN_READ_LINE;
	if (strcmp(name, "read_char") == 0) return BUILTIN_READ_CHAR;
	if (strcmp(name, "read_key") == 0) return BUILTIN_READ_KEY;
	if (strcmp(name, " why") == 0) return BUILTIN_RECOVERED;
	if (strcmp(name, "abs") == 0) return BUILTIN_ABS;
	if (strcmp(name, "close_to") == 0) return BUILTIN_CLOSE_TO;
	if (strcmp(name, "random") == 0) return BUILTIN_RANDOM;
	if (strcmp(name, "random_seed") == 0) return BUILTIN_RANDOM_SEED;
	if (strcmp(name, "exit") == 0) return BUILTIN_EXIT;
	if (strcmp(name, "input") == 0) return BUILTIN_INPUT;
	if (strcmp(name, "end_of_input") == 0) return BUILTIN_END_OF_INPUT;
	if (strcmp(name, "stdout") == 0) return BUILTIN_TO_STDOUT;
	if (strcmp(name, "stderr") == 0) return BUILTIN_TO_STDERR;
	static const struct { const char *name; Builtin b; } libs[] = {
		{ "files.read", BUILTIN_FILES_READ }, { "files.lines", BUILTIN_FILES_LINES },
		{ "files.write", BUILTIN_FILES_WRITE }, { "files.append", BUILTIN_FILES_APPEND },
		{ "files.exists", BUILTIN_FILES_EXISTS }, { "files.size", BUILTIN_FILES_SIZE },
		{ "files.copy", BUILTIN_FILES_COPY }, { "files.rename", BUILTIN_FILES_RENAME },
		{ "files.delete", BUILTIN_FILES_DELETE },
		{ "directories.list", BUILTIN_DIRS_LIST }, { "directories.all_files", BUILTIN_DIRS_ALL_FILES },
		{ "directories.exists", BUILTIN_DIRS_EXISTS }, { "directories.make", BUILTIN_DIRS_MAKE },
		{ "directories.remove", BUILTIN_DIRS_REMOVE }, { "directories.rename", BUILTIN_DIRS_RENAME },
		{ "directories.current", BUILTIN_DIRS_CURRENT }, { "directories.change_to", BUILTIN_DIRS_CHANGE_TO },
		{ "directories.home", BUILTIN_DIRS_HOME },
		{ "directories.save_folder", BUILTIN_DIRS_SAVE_FOLDER },
		{ "files.open", BUILTIN_FILES_OPEN }, { "files.open_to_write", BUILTIN_FILES_OPEN_TO_WRITE },
		{ "files.open_to_add", BUILTIN_FILES_OPEN_TO_ADD }, { "files.open_to_change", BUILTIN_FILES_OPEN_TO_CHANGE },
		{ "shell.run", BUILTIN_SHELL_RUN }, { "shell.status", BUILTIN_SHELL_STATUS },
		{ "shell.get", BUILTIN_SHELL_GET }, { "shell.has", BUILTIN_SHELL_HAS },
		{ "shell.set", BUILTIN_SHELL_SET }, { "shell.unset", BUILTIN_SHELL_UNSET },
		{ "addresses.of", BUILTIN_ADDRESSES_OF }, { "addresses.at", BUILTIN_ADDRESSES_AT },
		{ "addresses.none", BUILTIN_ADDRESSES_NONE },
		{ "addresses.allocate", BUILTIN_ADDRESSES_ALLOCATE }, { "addresses.free", BUILTIN_ADDRESSES_FREE },
		{ "addresses.reallocate", BUILTIN_ADDRESSES_REALLOCATE },
		{ "addresses.of_text", BUILTIN_ADDRESSES_OF_TEXT }, { "addresses.of_items", BUILTIN_ADDRESSES_OF_ITEMS },
		{ "events.run", BUILTIN_EVENTS_RUN }, { "events.stop", BUILTIN_EVENTS_STOP },
	};
	for (size_t i = 0; i < sizeof libs / sizeof *libs; i++)
		if (strcmp(name, libs[i].name) == 0)
			return libs[i].b;
	return BUILTIN_NONE;
}

/* ====================================================== names */

GlobalName *find_global(Checker *C, const char *name, SrcLoc use)
{
	for (int i = 0; i < C->names.len; i++) {
		GlobalName *g = &C->names.items[i];
		if (strcmp(g->name, name) != 0)
			continue;
		if (g->file_static && use.file >= 0 && g->file != use.file) {
			sema_error(use, "'%s' is declared static in %s, so it is private to that file",
				   name, diag_file_name(g->file));
			sema_note(g->loc, "'%s' is declared here", name);
		}
		return g;
	}
	return NULL;
}

TypeName *find_type(Checker *C, const char *name)
{
	for (int i = 0; i < C->types.len; i++)
		if (strcmp(C->types.items[i].name, name) == 0)
			return &C->types.items[i];
	return NULL;
}

static Type *builtin_type(const char *n)
{
	static const struct { const char *name; Type *t; } map[] = {
		{ "void", &t_void }, { "bool", &t_bool }, { "char", &t_char }, { "signed char", &t_schar },
		{ "unsigned char", &t_uchar }, { "short", &t_short }, { "unsigned short", &t_ushort },
		{ "int", &t_int }, { "unsigned int", &t_uint }, { "long", &t_long },
		{ "unsigned long", &t_ulong }, { "float", &t_float }, { "double", &t_double }, { "str", &t_str },
	};
	for (size_t i = 0; i < sizeof map / sizeof *map; i++)
		if (strcmp(map[i].name, n) == 0)
			return map[i].t;
	return NULL;
}

static Type *resolve_named(Checker *C, TypeExpr *te)
{
	if (te->builtin)
		return builtin_type(te->name);
	TypeName *tn = find_type(C, te->name);
	if (!tn && strcmp(te->name, "handler") == 0)
		return &t_handler;
	if (!tn && strcmp(te->name, "file") == 0)
		return &t_file;   /* registered as a type name by `use files` */
	if (!tn) {
		GlobalName *g = find_global(C, te->name, (SrcLoc){ -1, 0, 0 });
		if (g && g->kind == GN_FN)
			sema_error(te->loc, "'%s' is a function, not a type", te->name);
		else if (strcmp(te->name, "string") == 0 || strcmp(te->name, "String") == 0)
			sema_error(te->loc, "unknown type '%s'; the string type is 'str'", te->name);
		else
			sema_error(te->loc, "unknown type '%s'", te->name);
		return &t_error;
	}
	if (tn->type)
		return tn->type;
	if (tn->te) {   /* typedef, resolved on first use */
		if (tn->state == 1) {
			sema_error(tn->loc, "typedef '%s' refers to itself", tn->name);
			tn->type = &t_error;
			return &t_error;
		}
		tn->state = 1;
		Type *t = resolve_type(C, tn->te, 0, "typedefs");
		tn->state = 2;
		tn->type = t;
		return t;
	}
	return &t_error;
}

Type *resolve_type(Checker *C, TypeExpr *te, unsigned flags, const char *where)
{
	Type *t = &t_error;
	switch (te->kind) {
	case TYPEX_PRESET:
		return te->resolved;
	case TYPEX_NAMED:
		t = resolve_named(C, te);
		if (t->kind == TY_HANDLER && !(flags & (RT_ALLOW_REF | RT_ALLOW_HANDLER))) {
			sema_error(te->loc, "a handler can only be a parameter, or a field of an object (not one of %s)", where);
			t = &t_error;
		}
		if (t->kind == TY_CALLBACK && !(flags & RT_ALLOW_CALLBACK)) {
			sema_error(te->loc, "'%s' is a callback, for C: it can only be a parameter of an extern function "
				   "(not one of %s)", t->name, where);
			t = &t_error;
		}
		if (t->kind == TY_VOID && !(flags & RT_ALLOW_VOID)) {
			sema_error(te->loc, "'void' is only valid as a function return type");
			t = &t_error;
		}
		break;
	case TYPEX_REF:
	case TYPEX_SLICE: {
		bool slice = te->kind == TYPEX_SLICE;
		if (!(flags & (slice ? RT_ALLOW_SLICE : RT_ALLOW_REF))) {
			sema_error(te->loc, "references cannot be used in %s; a borrow may not outlive the "
				   "function that takes it", where);
			break;
		}
		Type *inner = resolve_type(C, te->inner, 0, slice ? "array elements" : where);
		if (type_is_error(inner))
			break;
		t = slice ? type_slice(C->A, inner, !te->is_const) : type_ref(C->A, inner, !te->is_const);
		break;
	}
	case TYPEX_ADDRESS: {
		Type *inner = te->inner ? resolve_type(C, te->inner, 0, "addresses") : NULL;
		if (inner && type_is_error(inner))
			break;
		if (inner && (!type_is_plain(inner) || inner->kind == TY_ERROR)) {
			sema_error(te->loc, "an address can only point at plain data (numbers, bool, char, other addresses, and "
				   "structs and arrays of those), not '%s'", ts(inner));
			break;
		}
		t = type_address(C->A, inner);
		break;
	}
	case TYPEX_LIST: {
		Type *inner = resolve_type(C, te->inner, 0, "list elements");
		if (type_is_error(inner))
			break;
		if (inner->kind == TY_OBJECT && inner->obj->kind == OBJ_UNION) {
			sema_error(te->loc, "a list can't hold unions (yet)");
			break;
		}
		t = type_list(C->A, inner);
		break;
	}
	case TYPEX_LOOKUP: {
		Type *key = resolve_type(C, te->key, 0, "lookup keys");
		Type *val = resolve_type(C, te->inner, 0, "lookup values");
		if (type_is_error(key) || type_is_error(val))
			break;
		if (key->kind != TY_INT && key->kind != TY_BOOL && key->kind != TY_STR) {
			sema_error(te->key->loc, "a lookup's keys must be whole numbers, chars, bools or strings, not '%s'%s",
				   ts(key), key->kind == TY_FLOAT ? " (decimals are rarely exactly equal)" : "");
			break;
		}
		if (val->kind == TY_OBJECT && val->obj->kind == OBJ_UNION) {
			sema_error(te->loc, "a lookup can't hold unions (yet)");
			break;
		}
		t = type_lookup(C->A, key, val);
		break;
	}
	case TYPEX_ARRAY: {
		Type *inner = resolve_type(C, te->inner, 0, "array elements");
		if (type_is_error(inner))
			break;
		if (!te->len) {
			if (!(flags & RT_ALLOW_UNSIZED)) {
				sema_error(te->loc, "this array needs a size");
				break;
			}
			t = type_array(C->A, inner, UINT64_MAX);   /* filled in from the initializer */
			break;
		}
		Type *lt = check_expr(C, &te->len, CTX_VALUE, NULL);
		int64_t n;
		if (type_is_error(lt))
			break;
		if (!type_is_int(lt) || !const_eval_int(C, te->len, &n)) {
			sema_error(te->len->loc, "an array size must be a constant integer");
			break;
		}
		if (n <= 0) {
			sema_error(te->len->loc, "an array size must be positive (found %lld)", (long long)n);
			break;
		}
		if ((uint64_t)n > ((uint64_t)1 << 40)) {
			sema_error(te->len->loc, "array size %lld is too large", (long long)n);
			break;
		}
		t = type_array(C->A, inner, (uint64_t)n);
		break;
	}
	}
	te->resolved = t;
	return t;
}

/* ====================================================== collection */

static void add_type(Checker *C, const char *name, SrcLoc loc, TypeName tn)
{
	TypeName *prev = find_type(C, name);
	if (prev) {
		sema_error(loc, "type '%s' is already defined", name);
		sema_note(prev->loc, "previous definition is here");
		return;
	}
	if (builtin_type(name)) {
		sema_error(loc, "'%s' is a built-in type name", name);
		return;
	}
	tn.name = name;
	tn.loc = loc;
	vec_push(C->A, C->types, tn);
}

static bool add_name(Checker *C, GlobalName g)
{
	for (int i = 0; i < C->names.len; i++) {
		GlobalName *p = &C->names.items[i];
		if (strcmp(p->name, g.name) != 0)
			continue;
		if (p->file_static && g.file_static && p->file != g.file)
			continue;   /* two files may each have a private name */
		sema_error(g.loc, "'%s' is already defined", g.name);
		sema_note(p->loc, "previous definition is here");
		return false;
	}
	if (find_type(C, g.name)) {
		sema_error(g.loc, "'%s' is already the name of a type", g.name);
		return false;
	}
	Builtin b = find_builtin(g.name);
	if (b && b != BUILTIN_ABS && b != BUILTIN_CLOSE_TO && b != BUILTIN_RANDOM && b != BUILTIN_RANDOM_SEED &&
	    b != BUILTIN_EXIT) {
		/* (those five give way to a program's own, or a header's: standard's abs and exit) */
		sema_error(g.loc, "'%s' is a built-in function", g.name);
		return false;
	}
	vec_push(C->A, C->names, g);
	return true;
}

static void collect_types(Checker *C)
{
	Sema *S = C->S;
	int nobj = 0;
	for (int i = 0; i < C->prog->decls.len; i++)
		if (C->prog->decls.items[i]->kind == DECL_OBJECT)
			nobj++;
	S->objects = arena_alloc(C->A, sizeof(ObjectInfo *) * (size_t)(nobj ? nobj : 1));

	for (int i = 0; i < C->prog->decls.len; i++) {
		Decl *d = C->prog->decls.items[i];
		if (d->kind == DECL_OBJECT) {
			ObjectDecl *od = d->as.object;
			ObjectInfo *o = arena_alloc(C->A, sizeof *o);
			o->decl = od;
			o->kind = od->kind;
			o->name = od->name;
			o->id = S->nobjects;
			o->file = od->loc.file;
			o->type = arena_alloc(C->A, sizeof *o->type);
			o->type->kind = TY_OBJECT;
			o->type->obj = o;
			od->ty = o->type;
			S->objects[S->nobjects++] = o;
			add_type(C, od->name, od->loc, (TypeName){ .type = o->type, .obj = o, .file = o->file });
		} else if (d->kind == DECL_ENUM) {
			EnumDecl *en = d->as.enum_;
			char *nm = arena_alloc(C->A, strlen(en->name) + 6);
			sprintf(nm, "enum %s", en->name);
			Type *t = type_int_named(C->A, &t_int, nm);
			t->methods = en->name;   /* weekday.text, if there is one, is its .text */
			add_type(C, en->name, en->loc, (TypeName){ .type = t, .file = en->loc.file });
		} else if (d->kind == DECL_TYPEDEF && d->as.typedef_.is_kind) {
			Type *b = resolve_type(C, d->as.typedef_.type, 0, "kinds");
			if (type_is_error(b))
				continue;
			if (!((b->kind == TY_INT && !b->is_char) || b->kind == TY_FLOAT) || b->kind_name || b->name) {
				sema_error(d->loc, "a kind is kept as a plain number (int, long, double...), not '%s'", ts(b));
				continue;
			}
			Type *k = arena_alloc(C->A, sizeof *k);
			*k = *b;
			k->name = k->kind_name = k->methods = d->as.typedef_.name;
			k->base = b;
			k->kind_strict = d->as.typedef_.is_strict;
			add_type(C, d->as.typedef_.name, d->loc, (TypeName){ .type = k, .file = d->loc.file });
		} else if (d->kind == DECL_TYPEDEF && d->as.typedef_.callback) {
			/* its shape is resolved once every type is known (collect_functions) */
			Type *t = arena_alloc(C->A, sizeof *t);
			t->kind = TY_CALLBACK;
			t->name = d->as.typedef_.name;
			t->sig = d->as.typedef_.callback;
			add_type(C, d->as.typedef_.name, d->loc, (TypeName){ .type = t, .file = d->loc.file });
		} else if (d->kind == DECL_TYPEDEF) {
			add_type(C, d->as.typedef_.name, d->loc,
				 (TypeName){ .te = d->as.typedef_.type, .file = d->loc.file });
		}
	}
}

/* `long kind moment, steps duration`: once every type is known */
static void collect_kind_steps(Checker *C)
{
	for (int i = 0; i < C->prog->decls.len; i++) {
		Decl *d = C->prog->decls.items[i];
		if (d->kind != DECL_TYPEDEF || !d->as.typedef_.is_kind || !d->as.typedef_.steps)
			continue;
		TypeName *tn = find_type(C, d->as.typedef_.name);
		if (!tn || !tn->type)
			continue;
		Type *st = resolve_type(C, d->as.typedef_.steps, 0, "kinds");
		if (type_is_error(st))
			continue;
		if (!((st->kind == TY_INT && !st->is_char) || st->kind == TY_FLOAT) || (st->kind_name && st->steps)) {
			sema_error(d->loc, "a kind steps by a number, or by a kind that doesn't step itself, not '%s'", ts(st));
			continue;
		}
		tn->type->steps = st;
	}
}

static void collect_enums(Checker *C)
{
	for (int i = 0; i < C->prog->decls.len; i++) {
		Decl *d = C->prog->decls.items[i];
		if (d->kind != DECL_ENUM)
			continue;
		EnumDecl *en = d->as.enum_;
		Type *et = find_type(C, en->name)->type;
		int64_t next = 0;
		for (int j = 0; j < en->items.len; j++) {
			EnumItem *it = &en->items.items[j];
			int64_t v = next;
			if (it->value) {
				Type *t = check_expr(C, &it->value, CTX_VALUE, NULL);
				if (!type_is_error(t) && (!type_is_int(t) || !const_eval_int(C, it->value, &v))) {
					sema_error(it->value->loc, "an enum value must be a constant integer");
					v = next;
				}
			}
			if (!int_fits(&t_int, v, false))
				sema_error(it->loc, "enum value %lld does not fit in an int", (long long)v);
			it->resolved = v;
			next = v + 1;
			add_name(C, (GlobalName){ .name = it->name, .kind = GN_ENUM_CONST, .value = v, .type = et,
						  .file = it->loc.file, .loc = it->loc });
		}
	}
}

/* ====================================================== functions */

static bool sig_equal(FnDecl *a, FnDecl *b)
{
	if (a->self_kind != b->self_kind || a->params.len != b->params.len || a->is_variadic != b->is_variadic)
		return false;
	if (!type_eq(a->ret_type, b->ret_type))
		return false;
	for (int i = 0; i < a->params.len; i++)
		if (!type_eq(a->params.items[i]->type->resolved, b->params.items[i]->type->resolved))
			return false;
	return true;
}

static bool extern_type_ok(Type *t, bool is_param)
{
	switch (t->kind) {
	case TY_BOOL: case TY_INT: case TY_FLOAT:
		return true;
	case TY_STR:
		return is_param;
	case TY_VOID:
		return !is_param;
	case TY_REF:
		return is_param && (t->inner->kind == TY_INT || t->inner->kind == TY_FLOAT || t->inner->kind == TY_BOOL);
	case TY_ADDRESS:
		return true;   /* a C pointer */
	case TY_CALLBACK:
		return is_param;   /* a C function pointer */
	default:
		return false;
	}
}

/* what a callback's functions can take and give, as C would: numbers of 32
 * bits or more, floats and doubles, and addresses (and nothing, given) */
static bool callback_type_ok(const Type *t, bool is_param)
{
	return (t->kind == TY_INT && t->bits >= 32 && !t->kind_name) || t->kind == TY_FLOAT || t->kind == TY_ADDRESS ||
	       (!is_param && t->kind == TY_VOID);
}

/* each callback's shape, once every type is known */
static void collect_callbacks(Checker *C)
{
	for (int i = 0; i < C->prog->decls.len; i++) {
		Decl *d = C->prog->decls.items[i];
		if (d->kind != DECL_TYPEDEF || !d->as.typedef_.callback)
			continue;
		FnDecl *sig = d->as.typedef_.callback;
		for (int k = 0; k < sig->params.len; k++) {
			VarDecl *p = sig->params.items[k];
			Type *t = resolve_type(C, p->type, 0, "a callback's parameters");
			if (!type_is_error(t) && !callback_type_ok(t, true))
				sema_error(p->type->loc, "a callback takes what C passes: numbers ('int', 'long', 'unsigned "
					   "int'...), 'double' or an address, not '%s'", ts(t));
		}
		Type *r = resolve_type(C, sig->ret, RT_ALLOW_VOID, "a callback's return type");
		if (!type_is_error(r) && !callback_type_ok(r, false))
			sema_error(sig->ret->loc, "a callback gives C a number of 32 bits or more, a 'double', an "
				   "address or nothing ('void'), not '%s'", ts(r));
		sig->ret_type = r;
	}
}

static void resolve_signature(Checker *C, FnDecl *fn)
{
	for (int i = 0; i < fn->params.len; i++) {
		VarDecl *p = fn->params.items[i];
		for (int j = 0; j < i; j++)
			if (strcmp(fn->params.items[j]->name, p->name) == 0)
				sema_error(p->loc, "parameter '%s' is declared twice", p->name);
		Type *t = resolve_type(C, p->type, RT_ALLOW_REF | RT_ALLOW_SLICE | (fn->is_extern ? RT_ALLOW_CALLBACK : 0),
				       fn->is_extern ? "parameters" : "a Cabby function's parameters");
		if (fn->is_extern && !type_is_error(t) && t->kind != TY_HANDLER && !extern_type_ok(t, true))
			sema_error(p->type->loc, "extern functions can take numbers, bool, char, str (passed as a C "
				   "string) or references to numbers, not '%s'", ts(t));
	}
	Type *r = resolve_type(C, fn->ret, RT_ALLOW_VOID, "return types");
	if (fn->is_extern && !type_is_error(r) && !extern_type_ok(r, false))
		sema_error(fn->ret->loc, "extern functions can only return numbers, bool, char or void");
	fn->ret_type = r;
}

static void collect_functions(Checker *C)
{
	collect_callbacks(C);
	for (int i = 0; i < C->prog->decls.len; i++) {
		Decl *d = C->prog->decls.items[i];
		if (d->kind != DECL_FN)
			continue;
		FnDecl *fn = d->as.fn;
		resolve_signature(C, fn);
		GlobalName *prev = NULL;
		for (int j = 0; j < C->names.len; j++) {
			GlobalName *g = &C->names.items[j];
			if (strcmp(g->name, fn->name) == 0 && !(g->file_static && d->is_static && g->file != fn->loc.file)) {
				prev = g;
				break;
			}
		}
		if (prev && prev->kind == GN_FN) {
			FnDecl *other = prev->decl;
			bool both_defs = !fn->is_prototype && prev->fn && !prev->fn->is_prototype;
			if (both_defs) {
				sema_error(fn->loc, "function '%s' is defined twice", fn->name);
				sema_note(prev->fn->loc, "first definition is here");
				continue;
			}
			if (fn->is_extern != other->is_extern) {
				sema_error(fn->loc, "'%s' is declared both as extern and as a Cabby function", fn->name);
				continue;
			}
			if (!sig_equal(fn, other)) {
				sema_error(fn->loc, "'%s' does not match its earlier declaration", fn->name);
				sema_note(other->loc, "earlier declaration is here");
				continue;
			}
			if (prev->file_static != d->is_static && !fn->is_prototype)
				prev->file_static = d->is_static || prev->file_static;
			if (!fn->is_prototype)
				prev->fn = fn;
			continue;
		}
		if (prev) {
			add_name(C, (GlobalName){ .name = fn->name, .loc = fn->loc });   /* reports the clash */
			continue;
		}
		add_name(C, (GlobalName){ .name = fn->name, .kind = GN_FN, .fn = fn, .decl = fn, .file = fn->loc.file,
					  .file_static = d->is_static, .loc = fn->loc });
	}
	/* prototypes must be defined somewhere */
	for (int i = 0; i < C->names.len; i++) {
		GlobalName *g = &C->names.items[i];
		if (g->kind != GN_FN || !g->fn->is_prototype || g->fn->is_extern)
			continue;
		sema_error(g->fn->loc, "'%s' is declared but never defined", g->name);
		sema_note(g->fn->loc, "add its body, or declare it 'extern' if it is a C function");
	}
	/* point every prototype at its definition */
	for (int i = 0; i < C->prog->decls.len; i++) {
		Decl *d = C->prog->decls.items[i];
		if (d->kind != DECL_FN)
			continue;
		GlobalName *g = NULL;
		for (int j = 0; j < C->names.len; j++)
			if (C->names.items[j].kind == GN_FN && strcmp(C->names.items[j].name, d->as.fn->name) == 0 &&
			    (!C->names.items[j].file_static || C->names.items[j].file == d->as.fn->loc.file))
				g = &C->names.items[j];
		if (g)
			d->as.fn->definition = g->fn;
	}
}

/* ====================================================== objects */

static ObjectInfo *info_of(ObjectDecl *d) { return d->ty ? d->ty->obj : NULL; }

static FnDecl *own_method(ObjectInfo *o, const char *name)
{
	for (int i = 0; i < o->decl->methods.len; i++) {
		FnDecl *m = o->decl->methods.items[i];
		if (!m->is_destructor && !m->is_maker && strcmp(m->name, name) == 0)
			return m;
	}
	return NULL;
}

FnDecl *lookup_method(ObjectInfo *o, const char *name)
{
	for (int i = 0; i < o->mro_len; i++) {
		FnDecl *m = own_method(o->mro[i], name);
		if (m)
			return m;
	}
	return NULL;
}

FieldInfo *lookup_field(ObjectInfo *o, const char *name)
{
	for (int i = 0; i < o->field_count; i++)
		if (strcmp(o->fields[i].name, name) == 0)
			return &o->fields[i];
	return NULL;
}

/* C3 linearization; reports cycles and inconsistent hierarchies. */
static bool compute_mro(Checker *C, ObjectInfo *o)
{
	if (o->visit == 2)
		return o->mro != NULL;
	if (o->visit == 1) {
		sema_error(o->decl->loc, "inheritance cycle: '%s' is (indirectly) its own parent", o->name);
		return false;
	}
	o->visit = 1;
	ParentVec *pv = &o->decl->parents;
	int np = pv->len;
	bool ok = true;
	for (int i = 0; i < np; i++)
		if (!pv->items[i].decl || !compute_mro(C, info_of(pv->items[i].decl)))
			ok = false;
	o->visit = 2;
	if (!ok)
		return false;

	int nl = np + 1;
	ObjectInfo ***lists = arena_alloc(C->A, sizeof(ObjectInfo **) * (size_t)nl);
	int *lens = arena_alloc(C->A, sizeof(int) * (size_t)nl);
	int *heads = arena_alloc(C->A, sizeof(int) * (size_t)nl);
	int total = 1;
	for (int i = 0; i < np; i++) {
		ObjectInfo *p = info_of(pv->items[i].decl);
		lists[i] = p->mro;
		lens[i] = p->mro_len;
		total += p->mro_len;
	}
	lists[np] = arena_alloc(C->A, sizeof(ObjectInfo *) * (size_t)(np ? np : 1));
	for (int i = 0; i < np; i++)
		lists[np][i] = info_of(pv->items[i].decl);
	lens[np] = np;

	ObjectInfo **out = arena_alloc(C->A, sizeof(ObjectInfo *) * (size_t)total);
	int n = 0;
	out[n++] = o;
	for (;;) {
		bool any_left = false;
		ObjectInfo *pick = NULL;
		for (int i = 0; i < nl && !pick; i++) {
			if (heads[i] >= lens[i])
				continue;
			any_left = true;
			ObjectInfo *cand = lists[i][heads[i]];
			bool in_tail = false;
			for (int j = 0; j < nl && !in_tail; j++)
				for (int k = heads[j] + 1; k < lens[j]; k++)
					if (lists[j][k] == cand) {
						in_tail = true;
						break;
					}
			if (!in_tail)
				pick = cand;
		}
		if (!any_left)
			break;
		if (!pick) {
			sema_error(o->decl->loc, "cannot compute a consistent method resolution order for '%s'; "
				   "its parents list shared ancestors in conflicting orders", o->name);
			return false;
		}
		out[n++] = pick;
		for (int i = 0; i < nl; i++)
			if (heads[i] < lens[i] && lists[i][heads[i]] == pick)
				heads[i]++;
	}
	o->mro = out;
	o->mro_len = n;
	return true;
}

static void layout_visit(ObjectInfo *k, bool *seen, ObjectInfo **out, int *n)
{
	if (seen[k->id])
		return;
	seen[k->id] = true;
	for (int i = 0; i < k->decl->parents.len; i++)
		layout_visit(info_of(k->decl->parents.items[i].decl), seen, out, n);
	out[(*n)++] = k;
}

/* Flattened layout plus (for structs/unions) C sizes and offsets. Field
 * types must already be resolved; nested struct sizes are computed on
 * demand (containment cycles are reported separately). */
void compute_object_layout(Checker *C, ObjectInfo *o)
{
	if (o->layout_done)
		return;
	o->layout_done = true;
	int N = C->S->nobjects;
	bool *seen = calloc((size_t)(N > 0 ? N : 1), sizeof *seen);
	o->layout = arena_alloc(C->A, sizeof(ObjectInfo *) * (size_t)(N > 0 ? N : 1));
	o->layout_len = 0;
	layout_visit(o, seen, o->layout, &o->layout_len);
	free(seen);
	for (int i = 0; i < o->layout_len; i++)
		if (o->layout[i] != o)
			o->layout[i]->has_derived = true;   /* (o is made from it) */

	o->block_start = arena_alloc(C->A, sizeof(int) * (size_t)(N > 0 ? N : 1));
	for (int i = 0; i < N; i++)
		o->block_start[i] = -1;
	int nf = 0;
	for (int i = 0; i < o->layout_len; i++) {
		o->block_start[o->layout[i]->id] = nf;
		nf += o->layout[i]->decl->fields.len;
	}
	o->fields = arena_alloc(C->A, sizeof(FieldInfo) * (size_t)(nf ? nf : 1));
	o->field_count = 0;
	for (int i = 0; i < o->layout_len; i++) {
		ObjectInfo *k = o->layout[i];
		for (int j = 0; j < k->decl->fields.len; j++) {
			VarDecl *fd = k->decl->fields.items[j];
			FieldInfo *fi = &o->fields[o->field_count];
			fi->name = fd->name;
			fi->type = fd->type->resolved ? fd->type->resolved : &t_error;
			fi->owner = k;
			fi->own_index = j;
			fi->slot = o->field_count;
			fi->loc = fd->loc;
			fi->is_inner = fd->is_inner;
			o->field_count++;
		}
	}
	for (int i = 0; i < o->layout_len; i++) {
		if (o->layout[i]->decl->drop_fn)
			o->has_user_drop = true;
		if (o->layout[i]->decl->make_fn)
			o->has_maker = true;
	}

	if (o->kind == OBJ_OBJECT) {
		o->size = o->align = 8;
		return;
	}
	uint64_t off = 0, align = 1, size = 0;
	o->visit = 3;   /* guard against self-containment while sizing */
	for (int i = 0; i < o->field_count; i++) {
		Type *ft = o->fields[i].type;
		const Type *e = ft;
		while (e->kind == TY_ARRAY)
			e = e->inner;
		if (e->kind == TY_OBJECT && e->obj != o && e->obj->visit != 3)
			compute_object_layout(C, e->obj);
		uint64_t a = type_align(ft), s = type_size(ft);
		if (a > align)
			align = a;
		if (o->kind == OBJ_UNION) {
			o->fields[i].offset = 0;
			if (s > size)
				size = s;
		} else {
			off = (off + a - 1) / a * a;
			o->fields[i].offset = off;
			off += s;
		}
	}
	o->visit = 0;
	if (o->kind == OBJ_STRUCT)
		size = off;
	o->align = align;
	o->size = (size + align - 1) / align * align;
}

static void check_field_conflicts(ObjectInfo *o)
{
	for (int a = 0; a < o->field_count; a++)
		for (int b = a + 1; b < o->field_count; b++) {
			FieldInfo *fa = &o->fields[a], *fb = &o->fields[b];
			if (strcmp(fa->name, fb->name) != 0)
				continue;
			bool already = false;
			for (int i = 0; i < o->decl->parents.len && !already; i++) {
				ObjectInfo *p = info_of(o->decl->parents.items[i].decl);
				if (p->block_start && p->block_start[fa->owner->id] >= 0 && p->block_start[fb->owner->id] >= 0)
					already = true;
			}
			if (already)
				continue;
			if (fa->owner == fb->owner) {
				sema_error(fb->loc, "field '%s' is declared twice in '%s'", fb->name, o->name);
			} else if (fb->owner == o) {
				sema_error(fb->loc, "field '%s' in '%s' conflicts with the field inherited from '%s'",
					   fb->name, o->name, fa->owner->name);
				sema_note(fa->loc, "inherited field declared here");
			} else {
				sema_error(o->decl->loc, "'%s' inherits two fields named '%s', from '%s' and '%s'",
					   o->name, fa->name, fa->owner->name, fb->owner->name);
				sema_note(fa->loc, "one is declared here");
				sema_note(fb->loc, "and the other here");
			}
		}
}

static bool contains_visit(Type *t)
{
	while (t->kind == TY_ARRAY)
		t = t->inner;
	if (t->kind != TY_OBJECT)
		return false;
	ObjectInfo *k = t->obj;
	if (k->visit == 1)
		return true;
	if (k->visit == 2)
		return false;
	k->visit = 1;
	for (int i = 0; i < k->field_count; i++)
		if (contains_visit(k->fields[i].type))
			return true;
	k->visit = 2;
	return false;
}

static void check_objects(Checker *C)
{
	Sema *S = C->S;
	int N = S->nobjects;

	for (int i = 0; i < N; i++) {
		ObjectInfo *o = S->objects[i];
		for (int j = 0; j < o->decl->parents.len; j++) {
			ParentRef *pr = &o->decl->parents.items[j];
			TypeName *tn = find_type(C, pr->name);
			if (!tn || !tn->obj) {
				sema_error(pr->loc, tn ? "'%s' is not an object" : "unknown parent object '%s'", pr->name);
			} else if (tn->obj->kind != OBJ_OBJECT) {
				sema_error(pr->loc, "'%s' is a %s; objects can only inherit from objects", pr->name,
					   ast_object_kind_name(tn->obj->kind));
			} else if (tn->obj == o) {
				sema_error(pr->loc, "'%s' cannot inherit from itself", o->name);
			} else {
				pr->decl = tn->obj->decl;
			}
		}
	}

	for (int i = 0; i < N; i++) {
		ObjectInfo *o = S->objects[i];
		for (int j = 0; j < o->decl->fields.len; j++) {
			VarDecl *f = o->decl->fields.items[j];
			Type *t = resolve_type(C, f->type, o->kind == OBJ_OBJECT ? RT_ALLOW_HANDLER : 0,
					       o->kind == OBJ_STRUCT ? "a struct's fields" : "a union's fields");
			if (f->is_const)
				sema_error(f->loc, "fields cannot be const; make the variable that holds the %s const instead",
					   ast_object_kind_name(o->kind));
			if (o->kind == OBJ_UNION && !type_is_error(t) && !type_is_plain(t))
				sema_error(f->loc, "union fields must be plain data (numbers, bool, char, and structs or "
					   "arrays of those), not '%s'", ts(t));
		}
	}

	for (int i = 0; i < N; i++) {
		ObjectInfo *o = S->objects[i];
		for (int j = 0; j < o->decl->methods.len; j++) {
			FnDecl *m = o->decl->methods.items[j];
			resolve_signature(C, m);
			if (m->is_destructor || m->is_maker)
				continue;
			for (int k = 0; k < j; k++)
				if (!o->decl->methods.items[k]->is_destructor && !o->decl->methods.items[k]->is_maker &&
				    strcmp(o->decl->methods.items[k]->name, m->name) == 0) {
					sema_error(m->loc, "method '%s' is defined twice in '%s'", m->name, o->name);
					sema_note(o->decl->methods.items[k]->loc, "first definition is here");
				}
		}
	}

	for (int i = 0; i < N; i++)
		compute_mro(C, S->objects[i]);
	for (int i = 0; i < N; i++)
		if (!S->objects[i]->mro)
			return;

	/* parents before children: MRO length is a valid topological key */
	ObjectInfo **order = malloc(sizeof *order * (size_t)(N > 0 ? N : 1));
	for (int i = 0; i < N; i++)
		order[i] = S->objects[i];
	for (int i = 1; i < N; i++)
		for (int j = i; j > 0 && order[j]->mro_len < order[j - 1]->mro_len; j--) {
			ObjectInfo *t = order[j];
			order[j] = order[j - 1];
			order[j - 1] = t;
		}
	for (int i = 0; i < N; i++)
		S->objects[i]->visit = 0;

	/* containment cycles first, so layout sizing never recurses forever */
	bool cyclic = false;
	for (int i = 0; i < N; i++) {
		ObjectInfo *o = S->objects[i];
		/* provisional flat field list for the containment walk */
		if (!o->fields) {
			int nf = 0;
			for (int k = 0; k < o->mro_len; k++)
				nf += o->mro[k]->decl->fields.len;
			o->fields = arena_alloc(C->A, sizeof(FieldInfo) * (size_t)(nf ? nf : 1));
			for (int k = 0; k < o->mro_len; k++)
				for (int j = 0; j < o->mro[k]->decl->fields.len; j++) {
					VarDecl *fd = o->mro[k]->decl->fields.items[j];
					o->fields[o->field_count++].type = fd->type->resolved ? fd->type->resolved : &t_error;
				}
		}
	}
	for (int i = 0; i < N; i++) {
		ObjectInfo *o = S->objects[i];
		for (int k = 0; k < N; k++)
			S->objects[k]->visit = 0;
		o->visit = 1;
		for (int j = 0; j < o->decl->fields.len; j++) {
			VarDecl *fd = o->decl->fields.items[j];
			if (fd->type->resolved && contains_visit(fd->type->resolved)) {
				sema_error(fd->loc, "'%s' contains itself through field '%s', so it would be infinitely large",
					   o->name, fd->name);
				cyclic = true;
				break;
			}
		}
	}
	for (int k = 0; k < N; k++) {
		S->objects[k]->visit = 0;
		S->objects[k]->fields = NULL;
		S->objects[k]->field_count = 0;
	}
	if (cyclic) {
		free(order);
		return;
	}
	for (int i = 0; i < N; i++) {
		compute_object_layout(C, order[i]);
		check_field_conflicts(order[i]);
	}
	free(order);

	for (int i = 0; i < N; i++) {
		ObjectInfo *o = S->objects[i];
		for (int j = 0; j < o->decl->methods.len; j++) {
			FnDecl *m = o->decl->methods.items[j];
			if (m->is_destructor || m->is_maker)
				continue;
			FieldInfo *f = lookup_field(o, m->name);
			if (f)
				sema_error(m->loc, "'%s' is both a field (from '%s') and a method of '%s'",
					   m->name, f->owner->name, o->name);
			for (int p = 0; p < o->decl->parents.len; p++) {
				FnDecl *base = lookup_method(info_of(o->decl->parents.items[p].decl), m->name);
				if (!base)
					continue;
				if (!m->overrides)
					m->overrides = base;
				if (m->is_inner != base->is_inner) {
					sema_error(m->loc, "'%s.%s' overrides '%s.%s', so it must be inner if that one is, and only then",
						   o->name, m->name, base->owner->name, base->name);
					sema_note(base->loc, "the overridden method is declared here");
				}
				if (!sig_equal(m, base)) {
					sema_error(m->loc, "'%s.%s' overrides '%s.%s' with a different signature",
						   o->name, m->name, base->owner->name, base->name);
					sema_note(base->loc, "the overridden method is declared here");
				}
			}
		}
		for (int a = 0; a < o->decl->parents.len; a++) {
			ObjectInfo *pa = info_of(o->decl->parents.items[a].decl);
			for (int k = 0; k < pa->mro_len; k++)
				for (int j = 0; j < pa->mro[k]->decl->methods.len; j++) {
					FnDecl *cand = pa->mro[k]->decl->methods.items[j];
					if (cand->is_destructor || cand->is_maker || own_method(o, cand->name))
						continue;
					FnDecl *ma = lookup_method(pa, cand->name);
					if (ma != cand)
						continue;
					for (int b = a + 1; b < o->decl->parents.len; b++) {
						FnDecl *mb = lookup_method(info_of(o->decl->parents.items[b].decl), cand->name);
						if (mb && mb != ma && !sig_equal(ma, mb)) {
							sema_error(o->decl->loc, "'%s' inherits incompatible methods named '%s' "
								   "from '%s' and '%s'; override it in '%s'",
								   o->name, cand->name, ma->owner->name, mb->owner->name, o->name);
							sema_note(ma->loc, "one is declared here");
							sema_note(mb->loc, "and the other here");
						}
					}
				}
		}
	}

	/* vtable slots: one per distinct instance-method name */
	int cap = 4;
	S->slot_names = arena_alloc(C->A, sizeof(char *) * (size_t)cap);
	for (int i = 0; i < N; i++) {
		ObjectDecl *od = S->objects[i]->decl;
		for (int j = 0; j < od->methods.len; j++) {
			FnDecl *m = od->methods.items[j];
			if (m->self_kind == SELF_NONE || m->is_destructor || m->is_maker)
				continue;
			int slot = -1;
			for (int s = 0; s < S->nslots; s++)
				if (strcmp(S->slot_names[s], m->name) == 0)
					slot = s;
			if (slot < 0) {
				if (S->nslots == cap) {
					const char **nn = arena_alloc(C->A, sizeof(char *) * (size_t)cap * 2);
					memcpy(nn, S->slot_names, sizeof(char *) * (size_t)S->nslots);
					S->slot_names = nn;
					cap *= 2;
				}
				slot = S->nslots;
				S->slot_names[S->nslots++] = m->name;
			}
			m->vtable_slot = slot;
		}
	}
	for (int i = 0; i < N; i++) {
		ObjectInfo *o = S->objects[i];
		o->vtable = arena_alloc(C->A, sizeof(FnDecl *) * (size_t)(S->nslots ? S->nslots : 1));
		for (int s = 0; s < S->nslots; s++) {
			FnDecl *m = lookup_method(o, S->slot_names[s]);
			o->vtable[s] = (m && m->self_kind != SELF_NONE) ? m : NULL;
		}
	}
}

/* ====================================================== globals */

static void collect_globals(Checker *C)
{
	for (int i = 0; i < C->prog->decls.len; i++) {
		Decl *d = C->prog->decls.items[i];
		if (d->kind != DECL_VAR)
			continue;
		for (int j = 0; j < d->as.vars.len; j++) {
			VarDecl *v = d->as.vars.items[j];
			C->cur_file = v->loc.file;
			Type *t = v->is_auto ? NULL : resolve_type(C, v->type, RT_ALLOW_UNSIZED, "global variables");
			Symbol *s = arena_alloc(C->A, sizeof *s);
			s->name = v->name;
			s->kind = SYM_GLOBAL;
			s->is_const = v->is_const;
			s->is_volatile = v->is_volatile;
			s->loc = v->loc;
			s->decl = v;
			s->file = v->loc.file;
			s->file_static = d->is_static;
			s->index = C->S->globals.len;
			v->sym = s;

			if (v->init) {
				Type *it = check_expr(C, &v->init, CTX_VALUE, t && t->kind == TY_ARRAY && t->len == UINT64_MAX ? NULL : t);
				if (!t)
					t = it;
				else if (t->kind == TY_ARRAY && t->len == UINT64_MAX)
					t = it;
				else if (!type_is_error(it))
					coerce(C, &v->init, t, "the initial value");
				if (!type_is_error(it) && !is_constant_expr(C, v->init))
					sema_error(v->init->loc, "a global's initial value must be a constant "
						   "(literals, enum constants and other constants, combined with operators)");
			}
			if (!t)
				t = &t_error;
			no_auto(v, t);
			if (t->kind == TY_STR && v->init && v->init->kind == EXPR_STRING)
				v->init->to_owned_str = false;   /* a global str holds the static literal */
			s->type = t;
			if (!type_is_error(t)) {
				if (t->kind == TY_STR && !v->is_const)
					sema_error(v->loc, "a global str must be const (nothing would own a changing string)");
				else if (t->kind == TY_STR && !(v->init && v->init->kind == EXPR_STRING))
					sema_error(v->loc, "a const str global needs a string literal as its value");
				else if (t->kind != TY_STR && !type_is_plain(t))
					sema_error(v->loc, "globals must be plain data (numbers, bool, char, enums, and structs, "
						   "unions or arrays of those), not '%s'", ts(t));
			}
			int64_t cv;
			if (v->is_const && type_is_int(t) && v->init && const_eval_int(C, v->init, &cv)) {
				s->has_const_value = true;
				s->const_value = cv;
			}
			vec_push(C->A, C->S->globals, s);
			add_name(C, (GlobalName){ .name = v->name, .kind = GN_GLOBAL, .sym = s, .file = s->file,
						  .file_static = d->is_static, .loc = v->loc });
		}
	}
}

/* ====================================================== entry point */

static void check_main(Checker *C)
{
	GlobalName *g = NULL;
	for (int i = 0; i < C->names.len; i++)
		if (strcmp(C->names.items[i].name, "main") == 0)
			g = &C->names.items[i];
	if (!g || g->kind != GN_FN) {
		diag_report_in(DIAG_ERROR, C->prog->files.len ? C->prog->files.items[C->prog->files.len - 1].id : -1, 0, 0,
			       "the program has no 'main' function");
		return;
	}
	FnDecl *m = g->fn;
	C->S->main_fn = m;
	if (m->is_extern) {
		sema_error(m->loc, "'main' cannot be extern");
		return;
	}
	if (m->ret_type && !type_is_error(m->ret_type) && m->ret_type->kind != TY_VOID &&
	    !type_eq(m->ret_type, &t_int))
		sema_error(m->loc, "'main' must return 'int' or 'void'");
	bool ok = m->params.len == 0;
	if (m->params.len == 2) {
		Type *a = m->params.items[0]->type->resolved, *b = m->params.items[1]->type->resolved;
		ok = a && b && (type_is_error(a) || type_eq(a, &t_int)) &&
		     (type_is_error(b) || (b->kind == TY_SLICE && b->inner->kind == TY_STR));
	}
	if (!ok)
		sema_error(m->loc, "'main' takes no parameters, or 'int argc, str argv[]'");
}

bool sema_check(Arena *arena, Program *prog, Sema *out)
{
	Checker C;
	memset(&C, 0, sizeof C);
	memset(out, 0, sizeof *out);
	C.A = arena;
	C.S = out;
	C.prog = prog;
	C.cur_file = -1;
	out->prog = prog;
	int errors_before = diag_error_count();

	collect_types(&C);
	collect_kind_steps(&C);
	collect_enums(&C);
	check_objects(&C);
	if (diag_error_count() != errors_before)
		return false;   /* bodies need a sound object model */
	collect_functions(&C);
	collect_globals(&C);
	check_main(&C);
	if (diag_error_count() != errors_before)
		return false;

	for (int i = 0; i < prog->decls.len; i++) {
		Decl *d = prog->decls.items[i];
		if (d->kind == DECL_FN) {
			if (d->as.fn->body)
				check_function(&C, d->as.fn);
		} else if (d->kind == DECL_OBJECT) {
			ObjectDecl *od = d->as.object;
			for (int j = 0; j < od->methods.len; j++)
				check_function(&C, od->methods.items[j]);
		}
	}
	check_handler_flows(&C);
	check_workers(&C);
	return diag_error_count() == errors_before;
}
