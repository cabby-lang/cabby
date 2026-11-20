/* sema_expr.c — stage 3, expressions: typing, conversions, borrows. */
#include <math.h>
#include "sema_int.h"
#include "diag.h"
#include "runtime/rt.h"

/* ====================================================== helpers */

static bool is_err(const Type *t) { return type_is_error(t); }

static Type *check_expr_inner(Checker *C, Expr **ep, Ctx ctx, Type *expected);

/* Rewrites `e` in place into a call of a sema-inserted builtin whose
 * arguments have already been checked. */
static void make_builtin(Checker *C, Expr *e, Builtin b, const char *name, Expr *a0, Expr *a1)
{
	SrcLoc l = e->loc;
	memset(&e->as, 0, sizeof e->as);
	e->kind = EXPR_CALL;
	Expr *callee = ast_expr(C->A, EXPR_IDENT, l);
	callee->as.ident.name = name;
	e->as.call.callee = callee;
	e->as.call.builtin = b;
	e->as.call.slot = -1;
	if (a0)
		vec_push(C->A, e->as.call.args, a0);
	if (a1)
		vec_push(C->A, e->as.call.args, a1);
}

Expr *make_implicit_cast(Checker *C, Expr *e, Type *to)
{
	Expr *c = ast_expr(C->A, EXPR_CAST, e->loc);
	c->as.cast.value = e;
	c->as.cast.implicit = true;
	c->ty = to;
	return c;
}

/* Converts *ep to `to` with an implicit cast node if the types differ. */
static void convert_to(Checker *C, Expr **ep, Type *to)
{
	Type *from = (*ep)->ty;
	if (is_err(from) || is_err(to) || type_eq(from, to))
		return;
	*ep = make_implicit_cast(C, *ep, to);
}

static int64_t wrap_int(int64_t v, const Type *t)
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

static ObjectInfo *object_with(const Type *t, const char *name);

/* ====================================================== literals */

Type *int_literal_type(Expr *e, bool negated)
{
	uint64_t v = e->as.int_lit.value;
	bool u = e->as.int_lit.is_unsigned, based = e->as.int_lit.based;
	int longs = e->as.int_lit.longs;
	if (negated && !u && v == (uint64_t)INT64_MAX + 1)
		return &t_long;   /* -9223372036854775808 */
	Type *cands[4];
	int n = 0;
	if (u) {
		if (!longs)
			cands[n++] = &t_uint;
		cands[n++] = &t_ulong;
	} else if (longs) {
		cands[n++] = &t_long;
		if (based || !negated)
			cands[n++] = &t_ulong;
	} else {
		cands[n++] = &t_int;
		if (based)
			cands[n++] = &t_uint;
		cands[n++] = &t_long;
		/* (a whole number too big for a long, but not for an unsigned
		 * long, is one: 18000000000000000000) */
		if (based || !negated)
			cands[n++] = &t_ulong;
	}
	for (int i = 0; i < n; i++)
		if (int_fits(cands[i], (int64_t)v, true))
			return cands[i];
	return NULL;
}

/* ====================================================== constants */

static bool ceval(Checker *C, Expr *e, int64_t *out);

bool const_eval_float(Checker *C, Expr *e, double *out)
{
	switch (e->kind) {
	case EXPR_FLOAT:
		*out = e->as.float_lit.value;
		if (e->ty && e->ty->kind == TY_FLOAT && e->ty->bits == 32)
			*out = (float)*out;
		return true;
	case EXPR_UNARY:
		if (e->as.unary.op == UN_NEG && const_eval_float(C, e->as.unary.operand, out)) {
			*out = -*out;
			return true;
		}
		return false;
	case EXPR_CAST: {
		Type *from = e->as.cast.value->ty;
		double d;
		int64_t i;
		if (!e->ty || e->ty->kind != TY_FLOAT)
			return false;
		if (from && from->kind == TY_FLOAT) {
			if (!const_eval_float(C, e->as.cast.value, &d))
				return false;
		} else {
			if (!ceval(C, e->as.cast.value, &i))
				return false;
			d = (from && from->kind == TY_INT && !from->is_signed) ? (double)(uint64_t)i : (double)i;
		}
		*out = e->ty->bits == 32 ? (double)(float)d : d;
		return true;
	}
	case EXPR_BINARY: {
		double a, b;
		if (!e->ty || e->ty->kind != TY_FLOAT || !const_eval_float(C, e->as.binary.lhs, &a) ||
		    !const_eval_float(C, e->as.binary.rhs, &b))
			return false;
		switch (e->as.binary.op) {
		case BIN_ADD: *out = a + b; break;
		case BIN_SUB: *out = a - b; break;
		case BIN_MUL: *out = a * b; break;
		case BIN_DIV: *out = a / b; break;
		default: return false;
		}
		if (e->ty->bits == 32)
			*out = (float)*out;
		return true;
	}
	default:
		return false;
	}
}

static bool ceval(Checker *C, Expr *e, int64_t *out)
{
	Type *t = e->ty;
	switch (e->kind) {
	case EXPR_INT:  *out = (int64_t)e->as.int_lit.value; return true;
	case EXPR_CHAR: *out = (int8_t)e->as.char_val; return true;
	case EXPR_BOOL: *out = e->as.bool_val; return true;
	case EXPR_IDENT: {
		Symbol *s = e->as.ident.sym;
		if (s && s->kind == SYM_GLOBAL && s->has_const_value) {
			*out = s->const_value;
			return true;
		}
		return false;
	}
	case EXPR_UNARY: {
		int64_t v;
		Expr *op = e->as.unary.operand;
		if (e->as.unary.op == UN_NEG && op->kind == EXPR_INT) {
			*out = (int64_t)(0 - op->as.int_lit.value);
			return true;
		}
		if (!ceval(C, op, &v))
			return false;
		switch (e->as.unary.op) {
		case UN_NEG:    *out = wrap_int((int64_t)(0 - (uint64_t)v), t); break;
		case UN_NOT:    *out = !v; break;
		case UN_BITNOT: *out = wrap_int(~v, t); break;
		}
		return true;
	}
	case EXPR_CAST: {
		Type *from = e->as.cast.value->ty;
		if (!t || (t->kind != TY_INT && t->kind != TY_BOOL))
			return false;
		if (from && from->kind == TY_FLOAT) {
			double d;
			if (!const_eval_float(C, e->as.cast.value, &d))
				return false;
			if (t->kind == TY_BOOL) {
				*out = d != 0;
				return true;
			}
			if (d != d || d >= 9.3e18 || d <= -9.3e18)
				return false;
			*out = wrap_int((int64_t)d, t);
			return true;
		}
		int64_t v;
		if (!ceval(C, e->as.cast.value, &v))
			return false;
		*out = t->kind == TY_BOOL ? v != 0 : wrap_int(v, t);
		return true;
	}
	case EXPR_BINARY: {
		int64_t a, b;
		BinaryOp op = e->as.binary.op;
		Type *ot = e->as.binary.lhs->ty;   /* operands share the converted type */
		if (!ot || ot->kind == TY_FLOAT || !ceval(C, e->as.binary.lhs, &a))
			return false;
		if (op == BIN_LOG_AND && !a) { *out = 0; return true; }
		if (op == BIN_LOG_OR && a)   { *out = 1; return true; }
		if (!ceval(C, e->as.binary.rhs, &b))
			return false;
		bool uns = ot->kind == TY_INT && !ot->is_signed;
		uint64_t ua = (uint64_t)a, ub = (uint64_t)b;
		int64_t r;
		switch (op) {
		case BIN_ADD: r = (int64_t)(ua + ub); break;
		case BIN_SUB: r = (int64_t)(ua - ub); break;
		case BIN_MUL: r = (int64_t)(ua * ub); break;
		case BIN_DIV:
		case BIN_MOD:
			if (b == 0) {
				sema_error(e->loc, "division by zero in a constant expression");
				return false;
			}
			if (uns)
				r = (int64_t)(op == BIN_DIV ? ua / ub : ua % ub);
			else if (a == INT64_MIN && b == -1)
				r = op == BIN_DIV ? a : 0;
			else
				r = op == BIN_DIV ? a / b : a % b;
			break;
		case BIN_BIT_AND: r = a & b; break;
		case BIN_BIT_OR:  r = a | b; break;
		case BIN_BIT_XOR: r = a ^ b; break;
		case BIN_SHL:
		case BIN_SHR:
			if (b < 0 || b >= (ot->bits ? ot->bits : 64)) {
				sema_error(e->loc, "shift amount %lld is out of range in a constant expression", (long long)b);
				return false;
			}
			if (op == BIN_SHL)
				r = (int64_t)(ua << b);
			else
				r = uns ? (int64_t)(ua >> b) : (a < 0 ? ~(~a >> b) : a >> b);
			break;
		case BIN_LOG_AND: case BIN_LOG_OR: r = b != 0; break;
		case BIN_EQ: r = a == b; break;
		case BIN_NE: r = a != b; break;
		case BIN_LT: r = uns ? ua < ub : a < b; break;
		case BIN_LE: r = uns ? ua <= ub : a <= b; break;
		case BIN_GT: r = uns ? ua > ub : a > b; break;
		case BIN_GE: r = uns ? ua >= ub : a >= b; break;
		default: return false;
		}
		*out = ast_binop_is_comparison(op) || ast_binop_is_logical(op) ? r : wrap_int(r, t);
		return true;
	}
	default:
		return false;
	}
}

bool const_eval_int(Checker *C, Expr *e, int64_t *out) { return ceval(C, e, out); }

bool is_constant_expr(Checker *C, Expr *e)
{
	int64_t i;
	double d;
	switch (e->kind) {
	case EXPR_STRING:
		return true;
	case EXPR_FLOAT:
		return true;
	case EXPR_INIT:
		for (int k = 0; k < e->as.init.entries.len; k++)
			if (!is_constant_expr(C, e->as.init.entries.items[k].value))
				return false;
		return true;
	default:
		if (e->ty && e->ty->kind == TY_FLOAT)
			return const_eval_float(C, e, &d);
		return ceval(C, e, &i);
	}
}

/* ====================================================== places */

typedef struct {
	Symbol *root;
	bool via_ref;    /* goes through a reference or slice held in root */
	bool mutable;
	bool ok;         /* false for temporaries */
} PlaceInfo;

static bool is_place_expr(const Expr *e)
{
	switch (e->kind) {
	case EXPR_IDENT: case EXPR_SELF:
		return e->as.ident.sym != NULL;
	case EXPR_FIELD:
	case EXPR_INDEX:
	case EXPR_AT:
		return true;
	default:
		return false;
	}
}

/* For a name that isn't known: what to write instead, when it's a C
 * function Cabby has its own way to do, or one a library header declares. */
static const char *name_hint(const char *n)
{
	static const struct { const char *name, *hint; } hints[] = {
		{ "puts", " (Cabby's own: println)" },
		{ "putchar", " (Cabby's own: print)" },
		{ "getchar", " (Cabby's own: read_char for a line's first character, read_key for one key without Enter)" },
		{ "atoi", " (Cabby's own: Text.to_int, after checking Text.is_int)" },
		{ "atol", " (Cabby's own: Text.to_long, after checking Text.is_long)" },
		{ "atof", " (Cabby's own: Text.to_double, after checking Text.is_number)" },
		{ "printf", " (add 'use standard' at the top of the file; or Cabby's own: println, and N.text 2 for 2 decimal places)" },
		{ "labs", " (Cabby's own: abs, for every kind of number)" },
		{ "fabs", " (Cabby's own: abs, for every kind of number)" },
		{ "rand", " (Cabby's own: random 1 to 6)" },
		{ "srand", " (Cabby's own: random_seed 42)" },
		{ "system", " (Cabby's own: shell.status or shell.run, with 'use shell'; or add 'use standard')" },
		{ "getenv", " (Cabby's own: shell.get, with 'use shell')" },
		{ "setenv", " (Cabby's own: shell.set, with 'use shell')" },
		{ "popen", " (Cabby's own: shell.run, with 'use shell')" },
		{ "strlen", " (Cabby's own: Text.length; or add 'use strings')" },
		{ "strcmp", " (Cabby's own: <, > and ==; or add 'use strings')" },
		{ "strncmp", " (add 'use strings' at the top of the file)" },
		{ "sqrt", " (add 'use math' at the top of the file)" },
		{ "pow", " (add 'use math' at the top of the file)" },
		{ "sin", " (add 'use math' at the top of the file)" },
		{ "cos", " (add 'use math' at the top of the file)" },
		{ "floor", " (add 'use math' at the top of the file)" },
		{ "ceil", " (add 'use math' at the top of the file)" },
		{ "round", " (add 'use math' at the top of the file)" },
		{ "files", " (add 'use files' at the top of the file)" },
		{ "directories", " (add 'use directories' at the top of the file)" },
		{ "shell", " (add 'use shell' at the top of the file)" },
		{ "processes", " (add 'use processes' at the top of the file)" },
		{ "addresses", " (add 'use addresses' at the top of the file)" },
		{ "events", " (add 'use events' at the top of the file)" },
		{ "timer", " (the type 'timer' needs 'use events' at the top of the file)" },
		{ "process", " (the type 'process' needs 'use processes' at the top of the file)" },
		{ "file", " (the type 'file' needs 'use files' at the top of the file)" },
		{ "directory", " (the type 'directory' needs 'use directories' at the top of the file)" },
		{ "gui", " (add 'use gui' at the top of the file)" },
		{ "gtk", " (add 'use gtk' at the top of the file)" },
		{ "tui", " (add 'use tui' at the top of the file)" },
		{ "game", " (add 'use game' at the top of the file)" },
		{ "tgame", " (add 'use tgame' at the top of the file)" },
		{ "world", " (add 'use world' at the top of the file)" },
		{ "sound", " (add 'use sound' at the top of the file)" },
	};
	for (size_t i = 0; i < sizeof hints / sizeof *hints; i++)
		if (strcmp(n, hints[i].name) == 0)
			return hints[i].hint;
	/* the rest of Cabby's libraries: a name that's one of theirs wants its
	 * `use` (archives.unpack without `use archives`) */
	static const char *const libraries[] = {
		"archives", "bluetooth", "brightness", "bytes", "databases", "dates", "dbus", "desktop", "emails", "hashes", "html", "images", "json", "linux",
		"markup", "math", "media", "mime", "networks", "os", "patterns", "power", "printing", "secrets", "speech", "standard", "strings", "system",
		"threads", "time", "users", "videos", "volume", "xml", "screens", "mouse", "keyboard", "csv", NULL };
	static char said[120];
	for (int i = 0; libraries[i]; i++)
		if (strcmp(n, libraries[i]) == 0) {
			snprintf(said, sizeof said, " (add 'use %s' at the top of the file)", n);
			return said;
		}
	return "";
}

static PlaceInfo analyze_place(const Expr *e)
{
	PlaceInfo pi = { 0 };
	switch (e->kind) {
	case EXPR_IDENT:
	case EXPR_SELF: {
		Symbol *s = e->as.ident.sym;
		if (!s)
			return pi;
		pi.root = s;
		pi.ok = true;
		if (s->type->kind == TY_REF || s->type->kind == TY_SLICE) {
			pi.via_ref = true;
			pi.mutable = s->type->is_mut;
		} else {
			pi.mutable = !s->is_const;
		}
		return pi;
	}
	case EXPR_FIELD:
		return analyze_place(e->as.field.object);
	case EXPR_INDEX:
		return analyze_place(e->as.index.base);
	case EXPR_AT:
		pi.mutable = true;   /* memory Cabby doesn't track: no root, no loans */
		return pi;
	default:
		return pi;
	}
}

/* `addresses.of X` where X is (part of) a variable of this function, which
 * is gone once it returns: that variable, or NULL. */
Symbol *address_of_local(const Expr *e)
{
	while (e->kind == EXPR_CAST)
		e = e->as.cast.value;
	if (e->kind != EXPR_CALL || e->as.call.builtin != BUILTIN_ADDRESSES_OF || e->as.call.args.len != 1)
		return NULL;
	PlaceInfo pi = analyze_place(e->as.call.args.items[0]);
	if (!pi.ok || !pi.root || pi.via_ref)
		return NULL;
	return pi.root->kind == SYM_LOCAL || pi.root->kind == SYM_PARAM ? pi.root : NULL;
}

/* Which part of its variable a place is: "" for all of it, or the fields
 * after it ("Things", "Shown.Id"). An element of a list or array counts
 * as the whole list. */
static const char *place_path(Checker *C, const Expr *e)
{
	switch (e->kind) {
	case EXPR_FIELD: {
		const char *base = place_path(C, e->as.field.object);
		size_t nb = strlen(base), nn = strlen(e->as.field.name);
		char *p = arena_alloc(C->A, nb + nn + 2);
		if (nb) {
			memcpy(p, base, nb);
			p[nb] = '.';
			memcpy(p + nb + 1, e->as.field.name, nn + 1);
		} else {
			memcpy(p, e->as.field.name, nn + 1);
		}
		return p;
	}
	case EXPR_INDEX:
		return place_path(C, e->as.index.base);
	default:
		return "";
	}
}

static void explain_immutable(const PlaceInfo *pi)
{
	Symbol *s = pi->root;
	if (s->kind == SYM_SELF)
		sema_note(s->loc, "this method is const, so it can't change the object");
	else if (pi->via_ref)
		sema_note(s->loc, "'%s' is a read-only borrow ('%s'); drop the 'const' to allow changes",
			  s->name, ts(s->type));
	else if (s->is_loop_var)
		sema_note(s->loc, "the 'for ... in' loop counts with '%s'; to change the counter yourself, "
			  "use a C-style for ('for int %s = 0; ...')", s->name, s->name);
	else
		sema_note(s->loc, "'%s' is declared const", s->name);
}

static void borrow_place(Checker *C, Expr *place, bool is_mut, SrcLoc loc, Symbol *binding)
{
	if (place->kind == EXPR_INDEX && place->as.index.on_str) {
		sema_error(loc, "a character of a string can't be borrowed; copy it into a 'char' variable");
		return;
	}
	PlaceInfo pi = analyze_place(place);
	if (!pi.ok)
		return;
	if (is_mut && !pi.mutable) {
		if (pi.root->kind == SYM_SELF)
			sema_error(loc, "can't change the object inside a const method");
		else if (pi.via_ref)
			sema_error(loc, "can't pass '%s' on as a mutable borrow: it is a read-only borrow", pi.root->name);
		else if (pi.root->is_loop_var)
			sema_error(loc, "can't lend '%s' as mutable: a 'for ... in' counter is read-only", pi.root->name);
		else
			sema_error(loc, "can't lend '%s' as mutable because it is const", pi.root->name);
		explain_immutable(&pi);
	}
	if (pi.root->is_register) {
		sema_error(loc, "can't borrow '%s' because it is declared 'register'", pi.root->name);
		sema_note(pi.root->loc, "declared here");
	}
	if (pi.root->kind == SYM_GLOBAL || C->unevaluated)
		return;   /* globals are plain data and never moved; no loan needed */
	if (place->kind == EXPR_IDENT || place->kind == EXPR_SELF)
		place->use = USE_BORROW;
	const char *path = place_path(C, place);
	record_access_at(C, pi.root, path, is_mut ? ACC_BORROW_MUT : ACC_BORROW, loc);
	if (pi.via_ref) {
		if (pi.root->type->is_mut)
			add_loan(C, pi.root, path, is_mut, loc, binding);   /* reborrow freezes the outer borrow */
		if (binding)
			add_source(C, binding, pi.root, loc);
	} else {
		add_loan(C, pi.root, path, is_mut, loc, binding);
	}
}

/* ====================================================== conversions */

static bool int_widens(const Type *from, const Type *to)
{
	if (from->kind == TY_BOOL)
		return true;
	if (from->is_signed == to->is_signed)
		return to->bits >= from->bits;
	if (!from->is_signed && to->is_signed)
		return to->bits > from->bits;
	return false;
}

static bool can_convert(Checker *C, Expr *e, Type *from, Type *to)
{
	if (to->kind == TY_BOOL && type_is_arith(from))
		return true;
	if (to->kind == TY_INT && (from->kind == TY_INT || from->kind == TY_BOOL)) {
		if (int_widens(from, to))
			return true;
		int64_t v;
		if (const_eval_int(C, e, &v)) {
			bool v_uns = from->kind == TY_INT && !from->is_signed && v < 0;
			return int_fits(to, v, v_uns);
		}
		return false;
	}
	if (to->kind == TY_FLOAT && (from->kind == TY_INT || from->kind == TY_BOOL))
		return true;
	if (to->kind == TY_FLOAT && from->kind == TY_FLOAT) {
		if (to->bits >= from->bits)
			return true;
		double d;
		return const_eval_float(C, e, &d);
	}
	return false;
}

/* A number written out in the program (15, -3, 60 * 60), not a variable or
 * a function's result: one a kind may take as its own. */
static bool written_number(const Expr *e)
{
	switch (e->kind) {
	case EXPR_INT:
	case EXPR_FLOAT:
		return true;
	case EXPR_UNARY:
		return e->as.unary.op == UN_NEG && written_number(e->as.unary.operand);
	case EXPR_BINARY: {
		BinaryOp op = e->as.binary.op;
		return (op == BIN_ADD || op == BIN_SUB || op == BIN_MUL || op == BIN_DIV) &&
		       written_number(e->as.binary.lhs) && written_number(e->as.binary.rhs);
	}
	case EXPR_CAST:
		return e->as.cast.implicit && written_number(e->as.cast.value);
	default:
		return false;
	}
}

/* Does this kind take written numbers as its own? Not a strict one, and not
 * one that steps (a date's number would be days since 1970). */
static bool kind_takes_numbers(const Type *t)
{
	return t->kind_name && !t->kind_strict && !t->steps;
}

static Type *check_args(Checker *C, Expr *call, FnDecl *fn, const char *fname);

bool coerce(Checker *C, Expr **ep, Type *to, const char *what)
{
	Expr *e = *ep;
	Type *from = e->ty;
	if (is_err(from) || is_err(to) || type_eq(from, to))
		return true;
	if (!from->kind_name && kind_takes_numbers(to) && type_is_arith(from) && from->kind != TY_BOOL &&
	    !(from->kind == TY_INT && from->is_char) && written_number(e)) {
		/* meters M = 15: a written number is taken as the kind */
		if (!can_convert(C, e, from, to->base)) {
			if (from->kind == TY_FLOAT && to->base->kind == TY_INT)
				sema_error(e->loc, "expected '%s' for %s; a '%s' is kept as a '%s', so it holds whole numbers",
					   ts(to), what, ts(to), ts(to->base));
			else
				sema_error(e->loc, "expected '%s' for %s; a '%s' is kept as a '%s', which can't hold this "
					   "number", ts(to), what, ts(to), ts(to->base));
			return false;
		}
		*ep = make_implicit_cast(C, e, to);
		return true;
	}
	if ((from->kind_name || to->kind_name) && type_is_arith(from) && type_is_arith(to)) {
		sema_error(e->loc, "expected '%s' for %s, found '%s'", ts(to), what, ts(from));
		if (from->kind_name && to->kind_name)
			sema_note(e->loc, "a '%s' and a '%s' are different kinds of value", ts(from), ts(to));
		else
			if (to->kind_name && strcmp(to->kind_name, "duration") == 0)
				sema_note(e->loc, "say how long: time.milliseconds(500), time.seconds(2), time.minutes(1)...");
			else
				sema_note(e->loc, "a '%s' is its own kind of value, and doesn't mix with plain numbers%s; a cast "
					  "turns one into the other: (%s)...", ts(from->kind_name ? from : to),
					  to->kind_name && to->kind_strict ? " (it is strict: not even written ones)"
					  : to->kind_name && !to->steps ? " held in variables" : "", ts(to));
		return false;
	}
	if (type_is_arith(from) && type_is_arith(to) && can_convert(C, e, from, to)) {
		*ep = make_implicit_cast(C, e, to);
		return true;
	}
	if (from->kind == TY_ARRAY && to->kind == TY_ARRAY && from->len == to->len && type_eq(from->inner, to->inner))
		return true;
	if (from->kind == TY_OBJECT && to->kind == TY_OBJECT && from->obj->kind == OBJ_OBJECT && to->obj->kind == OBJ_OBJECT &&
	    object_is_subclass(from->obj, to->obj)) {
		/* a square as a shape: the same object, used as one (its own
		 * methods still the ones called) */
		*ep = make_implicit_cast(C, e, to);
		return true;
	}
	if (from->kind == TY_ADDRESS && to->kind == TY_ADDRESS && !to->inner)
		return true;   /* any address is an address of something (C's void *) */
	if (from->kind == TY_ADDRESS && to->kind == TY_ADDRESS) {
		sema_error(e->loc, "expected '%s' for %s, found '%s'; to read the memory as another kind, cast it: "
			   "(%s)...", ts(to), what, ts(from), ts(to));
		return false;
	}

	if (from->kind == TY_STR && type_is_arith(to) && to->kind != TY_BOOL) {
		const char *m = to->kind == TY_FLOAT ? ".to_double" : to->bits > 32 ? ".to_long" : ".to_int";
		sema_error(e->loc, "expected '%s' for %s, found 'str'; to read a number from text, "
			   "use %s (and check first with %s)", ts(to), what, m,
			   to->kind == TY_FLOAT ? ".is_number" : to->bits > 32 ? ".is_long" : ".is_int");
		return false;
	}
	if (to->kind == TY_OBJECT && to->obj->kind == OBJ_OBJECT &&
	    (from->kind == TY_STR || from->kind == TY_BOOL || type_is_arith(from)) && !from->kind_name) {
		/* json X = "Ada": a library's conversion, the function <type>.from_<kind of value> */
		const char *u = from->kind == TY_STR ? "str" : from->kind == TY_BOOL ? "bool"
				: from->kind == TY_FLOAT ? "double" : from->is_char ? NULL : "long";
		char full[256];
		snprintf(full, sizeof full, "%s.from_%s", to->obj->name, u ? u : "");
		GlobalName *g = u ? find_global(C, full, e->loc) : NULL;
		if (g && g->kind == GN_FN) {
			Expr *call = ast_expr(C->A, EXPR_CALL, e->loc);
			Expr *callee = ast_expr(C->A, EXPR_IDENT, e->loc);
			callee->as.ident.name = arena_strndup(C->A, full, strlen(full));
			call->as.call.callee = callee;
			call->as.call.slot = -1;
			call->as.call.fn = g->fn;
			call->as.call.checked_args = 1;
			vec_push(C->A, call->as.call.args, e);
			call->ty = check_args(C, call, g->fn, callee->as.ident.name);
			*ep = call;
			return !is_err(call->ty);
		}
	}
	if (e->kind == EXPR_INT && to->kind == TY_INT && from->kind == TY_INT) {
		/* a number written out that's too big for it: no cast makes it fit */
		const char *name = ts(to);
		sema_error(e->loc, "%llu is too big for %s, %s '%s'", (unsigned long long)e->as.int_lit.value, what,
			   strchr("aeiou", name[0]) ? "an" : "a", name);
		sema_note(e->loc, "it fits in '%s'", ts(from));
		return false;
	}
	sema_error(e->loc, "expected '%s' for %s, found '%s'", ts(to), what, ts(from));
	if (from->kind == TY_FLOAT && to->kind == TY_INT)
		sema_note(e->loc, "this would drop the fraction; write a cast if you mean it: (%s)...", ts(to));
	else if (type_is_arith(from) && type_is_arith(to))
		sema_note(e->loc, "'%s' can't hold every '%s' value; write a cast if you are sure it fits: (%s)...",
			  ts(to), ts(from), ts(to));
	else if (from->kind == TY_OBJECT && to->kind == TY_OBJECT && object_is_subclass(from->obj, to->obj))
		sema_note(e->loc, "a '%s' can't be turned into a '%s' by value; take the parameter as a "
			  "reference ('const %s &') to use it as one", from->obj->name, to->obj->name, to->obj->name);
	return false;
}

/* Binds a reference (or slice) of type `rt` to *ep, wrapping it in EXPR_BORROW. */
/* set for one bind_ref: the expression was checked already (the value a
 * type's method is called on, or a value being converted) */
static bool g_ref_prechecked;

bool bind_ref(Checker *C, Expr **ep, Type *rt, Symbol *binding, const char *what)
{
	Expr *a = *ep;
	bool place = (a->kind == EXPR_IDENT || a->kind == EXPR_SELF || a->kind == EXPR_FIELD || a->kind == EXPR_INDEX);
	bool prechecked = g_ref_prechecked;
	g_ref_prechecked = false;
	Type *at = prechecked ? a->ty : check_expr(C, ep, place ? CTX_PLACE : CTX_READ, rt->inner);
	a = *ep;
	place = is_place_expr(a);
	if (is_err(at))
		return false;

	bool ok;
	if (rt->kind == TY_SLICE) {
		ok = (at->kind == TY_ARRAY || at->kind == TY_SLICE) && type_eq(at->inner, rt->inner);
	} else {
		ok = type_eq(at, rt->inner) ||
		     (at->kind == TY_OBJECT && rt->inner->kind == TY_OBJECT && at->obj->kind == OBJ_OBJECT &&
		      object_is_subclass(at->obj, rt->inner->obj));
	}
	/* `const bytes &Data` given "Hello": the library's conversion
	 * (bytes.from_str) makes a temporary, which is lent */
	if (!ok && rt->kind == TY_REF && !rt->is_mut && !binding && rt->inner->kind == TY_OBJECT &&
	    rt->inner->obj->kind == OBJ_OBJECT && !at->kind_name &&
	    (at->kind == TY_STR || at->kind == TY_BOOL || (type_is_arith(at) && !at->is_char))) {
		const char *u = at->kind == TY_STR ? "str" : at->kind == TY_BOOL ? "bool" : at->kind == TY_FLOAT ? "double" : "long";
		char full[256];
		snprintf(full, sizeof full, "%s.from_%s", rt->inner->obj->name, u);
		GlobalName *g = find_global(C, full, a->loc);
		if (g && g->kind == GN_FN) {
			if (!coerce(C, ep, rt->inner, what))
				return false;
			a = *ep;
			at = a->ty;
			place = false;
			ok = true;
		}
	}
	if (!ok) {
		sema_error(a->loc, "expected '%s' for %s, found '%s'", ts(rt->inner), what, ts(at));
		if (rt->kind == TY_SLICE)
			sema_note(a->loc, "this parameter borrows an array of '%s'", ts(rt->inner));
		else if (type_is_arith(at) && type_is_arith(rt->inner))
			sema_note(a->loc, "a reference must point at a value of exactly the right type; there is no "
				  "conversion through a borrow");
		return false;
	}
	if (!place) {
		if (rt->is_mut) {
			sema_error(a->loc, "can't lend a temporary value as mutable for %s; store it in a variable first", what);
			return false;
		}
		if (binding) {
			sema_error(a->loc, "can't keep a reference to a temporary value; store the value in a variable first");
			return false;
		}
	} else {
		borrow_place(C, a, rt->is_mut, a->loc, binding);
	}
	Expr *b = ast_expr(C->A, EXPR_BORROW, a->loc);
	b->as.borrow.operand = a;
	b->as.borrow.is_mut = rt->is_mut;
	b->ty = rt;
	*ep = b;
	return true;
}

/* ====================================================== operators */

static Type *require_int(Expr *e, Type *t, const char *op)
{
	if (is_err(t))
		return t;
	if (t->kind == TY_INT || t->kind == TY_BOOL)
		return t;
	if (t->kind == TY_FLOAT)
		sema_error(e->loc, "'%s' needs integer operands, not '%s'", op, ts(t));
	else
		sema_error(e->loc, "'%s' needs numbers, not '%s'", op, ts(t));
	return &t_error;
}

static void sign_compare_warning(Checker *C, Expr *e, Expr *l, Expr *r, Type *common)
{
	if (common->kind != TY_INT || common->is_signed)
		return;
	Expr *sides[2] = { l, r };
	for (int i = 0; i < 2; i++) {
		Type *t = sides[i]->ty;
		if (t->kind != TY_INT || !t->is_signed || t->bits >= common->bits + 1)
			continue;
		int64_t v;
		if (const_eval_int(C, sides[i], &v) && v >= 0)
			continue;
		sema_warning(e->loc, "comparing signed '%s' with unsigned '%s': a negative value would compare as a "
			     "huge number", ts(t), ts(sides[1 - i]->ty));
		return;
	}
}

/* A side of `==` that rounding is likely to have touched: a decimal with a
 * fraction (0.3, which binary can't hold exactly), or floating-point
 * arithmetic. Whole numbers (0.0, 1.0) and plain variables are left alone,
 * since comparing those exactly is often what's meant. */
static bool rounded_side(Expr *e)
{
	while (e->kind == EXPR_CAST && e->as.cast.implicit)
		e = e->as.cast.value;
	if (e->kind == EXPR_UNARY && e->as.unary.op == UN_NEG)
		e = e->as.unary.operand;
	if (e->kind == EXPR_FLOAT)
		return e->as.float_lit.value != floor(e->as.float_lit.value);
	if (e->kind == EXPR_BINARY && e->ty && e->ty->kind == TY_FLOAT) {
		BinaryOp b = e->as.binary.op;
		return b == BIN_ADD || b == BIN_SUB || b == BIN_MUL || b == BIN_DIV || b == BIN_MOD;
	}
	return false;
}

static void float_equal_warning(Expr *e, BinaryOp op, Expr *l, Expr *r, Type *common)
{
	if (common->kind != TY_FLOAT || !(rounded_side(l) || rounded_side(r)))
		return;
	sema_warning(e->loc, "floating-point numbers are rounded, so two that look the same may not be exactly "
		     "equal; use %s", op == BIN_EQ ? "'close_to A, B'" : "'!close_to(A, B)'");
}

static Type *check_address_binary(Checker *C, Expr *e, BinaryOp op, Expr **lp, Expr **rp, Type *lt, Type *rt);

/* What `lt op rt` gives when a kind is involved, or NULL if it can't be
 * done. A plain kind (a duration) adds to and takes from its own kind, and
 * is multiplied and divided by plain numbers; a kind that steps (a moment,
 * by durations) takes steps, and two of them give the step between. */
static Type *kind_result(BinaryOp op, Type *lt, Type *rt, bool *ratio)
{
	*ratio = false;
	bool lk = lt->kind_name != NULL, rk = rt->kind_name != NULL;
	bool lplain = !lk && type_is_arith(lt) && lt->kind != TY_BOOL;
	bool rplain = !rk && type_is_arith(rt) && rt->kind != TY_BOOL;
	if (ast_binop_is_comparison(op))
		return lk && rk && type_eq(lt, rt) ? &t_bool : NULL;
	/* stepping */
	if (lk && lt->steps && (op == BIN_ADD || op == BIN_SUB) &&
	    (type_eq(rt, lt->steps) || (!lt->steps->kind_name && rplain && rt->kind == TY_INT)))
		return lt;
	if (rk && rt->steps && op == BIN_ADD &&
	    (type_eq(lt, rt->steps) || (!rt->steps->kind_name && lplain && lt->kind == TY_INT)))
		return rt;
	if (lk && lt->steps && op == BIN_SUB && type_eq(lt, rt))
		return lt->steps;
	if ((lk && lt->steps) || (rk && rt->steps))
		return NULL;
	/* plain kinds */
	if (lk && rk && type_eq(lt, rt)) {
		if (op == BIN_ADD || op == BIN_SUB || op == BIN_MOD)
			return lt;
		if (op == BIN_DIV) {
			*ratio = true;   /* how many times one goes into the other */
			return &t_double;
		}
		return NULL;
	}
	if (lk && rplain && (op == BIN_MUL || op == BIN_DIV))
		return lt;
	if (rk && lplain && op == BIN_MUL)
		return rt;
	return NULL;
}

static Type *kind_base(Type *t) { return t->kind_name ? t->base : t; }

static Type *check_kind_binary(Checker *C, Expr *e, BinaryOp op, Expr **lp, Expr **rp, Type *lt, Type *rt)
{
	if (ast_binop_is_logical(op) || ast_binop_is_bitwise(op) || op == BIN_SHL || op == BIN_SHR) {
		sema_error(e->loc, "'%s' can't be used with '%s' and '%s'", ast_binop_str(op), ts(lt), ts(rt));
		return &t_error;
	}
	bool ratio;
	bool as_kind = op == BIN_ADD || op == BIN_SUB || op == BIN_MOD || ast_binop_is_comparison(op);
	if (as_kind && kind_takes_numbers(lt) && !rt->kind_name && written_number(*rp) && type_is_arith(rt) &&
	    rt->kind != TY_BOOL && can_convert(C, *rp, rt, lt->base)) {
		*rp = make_implicit_cast(C, *rp, lt);   /* Walk + 50: fifty meters */
		rt = lt;
	} else if (as_kind && kind_takes_numbers(rt) && !lt->kind_name && written_number(*lp) && type_is_arith(lt) &&
		   lt->kind != TY_BOOL && can_convert(C, *lp, lt, rt->base)) {
		*lp = make_implicit_cast(C, *lp, rt);
		lt = rt;
	}
	Type *r = kind_result(op, lt, rt, &ratio);
	if (!r) {
		sema_error(e->loc, "'%s' can't be used with '%s' and '%s'", ast_binop_str(op), ts(lt), ts(rt));
		if (lt->kind_name && rt->kind_name && !type_eq(lt, rt))
			sema_note(e->loc, "they are different kinds of value");
		else if ((lt->kind_name && !rt->kind_name) || (!lt->kind_name && rt->kind_name)) {
			Type *k = lt->kind_name ? lt : rt;
			sema_note(e->loc, "a '%s' is its own kind of value, and doesn't mix with plain numbers that way%s", ts(k),
				  k->kind_strict ? " (it is strict: not even written ones)" : "");
		}
		return &t_error;
	}
	/* worked out as the plain numbers they are kept as */
	convert_to(C, lp, kind_base(lt));
	convert_to(C, rp, kind_base(rt));
	Type *c = ratio ? &t_double : type_common(kind_base(lt), kind_base(rt));
	convert_to(C, lp, c);
	convert_to(C, rp, c);
	if (r == &t_bool || type_eq(kind_base(r), c))
		return r;
	/* e.g. a duration times 1.5: worked out as a double, kept as a long */
	Expr *bin = ast_expr(C->A, EXPR_BINARY, e->loc);
	*bin = *e;
	bin->ty = c;
	memset(&e->as, 0, sizeof e->as);
	e->kind = EXPR_CAST;
	e->as.cast.value = bin;
	e->as.cast.implicit = true;
	return r;
}

static Type *check_binary(Checker *C, Expr *e)
{
	BinaryOp op = e->as.binary.op;
	Expr **lp = &e->as.binary.lhs, **rp = &e->as.binary.rhs;
	const char *o = ast_binop_str(op);

	if (ast_binop_is_logical(op)) {
		check_cond(C, lp, "the left side of a logical operator");
		check_cond(C, rp, "the right side of a logical operator");
		return &t_bool;
	}

	/* + only reads its operands too: it may be joining strings */
	Ctx ctx = (ast_binop_is_comparison(op) || op == BIN_ADD) ? CTX_READ : CTX_VALUE;
	Type *lt = check_expr(C, lp, ctx, NULL);
	/* A string on the left is held (borrowed) while the right side is
	 * evaluated, and released as soon as the comparison is done. */
	int mark = C->temps.len;
	if (ctx == CTX_READ && !is_err(lt) && !type_is_copy(lt) && is_place_expr(*lp))
		borrow_place(C, *lp, false, (*lp)->loc, NULL);
	/* `A is addresses.none`: the right side's kind of address is the left's */
	Type *rt = check_expr(C, rp, ctx, !is_err(lt) && lt->kind == TY_ADDRESS ? lt : NULL);
	release_temps(C, mark);
	if (is_err(lt) || is_err(rt))
		return &t_error;
	if (lt->kind == TY_ADDRESS || rt->kind == TY_ADDRESS)
		return check_address_binary(C, e, op, lp, rp, lt, rt);
	if (lt->kind_name || rt->kind_name)
		return check_kind_binary(C, e, op, lp, rp, lt, rt);

	if (op == BIN_EQ || op == BIN_NE) {
		if (lt->kind == TY_STR && rt->kind == TY_STR)
			return &t_bool;
		if (type_is_arith(lt) && type_is_arith(rt)) {
			Type *c = type_common(lt, rt);
			sign_compare_warning(C, e, *lp, *rp, c);
			float_equal_warning(e, op, *lp, *rp, c);
			convert_to(C, lp, c);
			convert_to(C, rp, c);
			return &t_bool;
		}
		if (lt->kind == TY_OBJECT && rt->kind == TY_OBJECT && lt->obj == rt->obj && object_with(lt, "same_as"))
			sema_error(e->loc, "'%s' can't compare two '%s's; use .same_as: 'A.same_as(B)'", o, ts(lt));
		else if (lt->kind == TY_OBJECT || rt->kind == TY_OBJECT)
			sema_error(e->loc, "'%s' can't compare %ss; compare their fields", o,
				   ast_object_kind_name((lt->kind == TY_OBJECT ? lt : rt)->obj->kind));
		else
			sema_error(e->loc, "can't compare '%s' with '%s'", ts(lt), ts(rt));
		return &t_error;
	}
	if (ast_binop_is_comparison(op)) {
		if (type_is_arith(lt) && type_is_arith(rt)) {
			Type *c = type_common(lt, rt);
			sign_compare_warning(C, e, *lp, *rp, c);
			convert_to(C, lp, c);
			convert_to(C, rp, c);
			return &t_bool;
		}
		if (lt->kind == TY_STR && rt->kind == TY_STR) {
			/* dictionary order: compare(a, b) < 0 */
			Expr *cmp = ast_expr(C->A, EXPR_CALL, e->loc);
			make_builtin(C, cmp, BUILTIN_STR_COMPARE, "compare", *lp, *rp);
			cmp->ty = &t_int;
			Expr *zero = ast_expr(C->A, EXPR_INT, e->loc);
			zero->ty = &t_int;
			*lp = cmp;
			*rp = zero;
			return &t_bool;
		}
		else
			sema_error(e->loc, "'%s' needs numbers, found '%s' and '%s'%s", o, ts(lt), ts(rt),
				   lt->kind == TY_STR || rt->kind == TY_STR ? " (two strings can be compared, in dictionary order)" : "");
		return &t_error;
	}
	if (op == BIN_ADD && (lt->kind == TY_STR || rt->kind == TY_STR)) {
		/* joining: a str with a str or a char, either way round */
		bool lok = lt->kind == TY_STR || (lt->kind == TY_INT && lt->is_char);
		bool rok = rt->kind == TY_STR || (rt->kind == TY_INT && rt->is_char);
		if (!lok || !rok) {
			const Type *other = lt->kind == TY_STR ? rt : lt;
			sema_error(e->loc, "'+' joins a 'str' with a 'str' or a 'char', not '%s'", ts(other));
			if (type_is_arith(other) || other->kind == TY_BOOL)
				sema_note(e->loc, "to put a number into text, use its .text: '\"total: \" + n.text'");
			return &t_error;
		}
		make_builtin(C, e, BUILTIN_STR_CONCAT, "+", *lp, *rp);
		return &t_str;
	}
	if (!type_is_arith(lt) || !type_is_arith(rt)) {
		sema_error(e->loc, "'%s' needs numbers, found '%s' and '%s'", o, ts(lt), ts(rt));
		return &t_error;
	}
	if (op == BIN_SHL || op == BIN_SHR) {
		if (is_err(require_int(*lp, lt, o)) || is_err(require_int(*rp, rt, o)))
			return &t_error;
		Type *res = type_promote(lt);
		convert_to(C, lp, res);
		convert_to(C, rp, type_promote(rt));
		return res;
	}
	if (ast_binop_is_bitwise(op) || op == BIN_MOD) {
		if (is_err(require_int(*lp, lt, o)) || is_err(require_int(*rp, rt, o)))
			return &t_error;
	}
	Type *c = type_common(lt, rt);
	convert_to(C, lp, c);
	convert_to(C, rp, c);
	return c;
}

Type *check_cond(Checker *C, Expr **ep, const char *what)
{
	Type *t = check_expr(C, ep, CTX_VALUE, NULL);
	if (is_err(t))
		return t;
	if (t->kind == TY_BOOL)
		return t;
	if (type_is_arith(t)) {
		*ep = make_implicit_cast(C, *ep, &t_bool);   /* non-zero is true, as in C */
		return &t_bool;
	}
	sema_error((*ep)->loc, "%s must be a number or bool, not '%s'", what, ts(t));
	return &t_error;
}

/* ====================================================== identifiers */

static GlobalUse *global_use_in(Checker *C, FnDecl *fn);

static void ident_use(Checker *C, Expr *e, Symbol *s, Ctx ctx)
{
	bool through = s->type->kind == TY_REF;
	Type *vt = through ? s->type->inner : s->type;
	if (s->kind == SYM_GLOBAL && !s->is_const && !s->has_const_value && C->fn && !C->unevaluated &&
	    !global_use_in(C, C->fn))
		vec_push(C->A, C->global_uses, ((GlobalUse){ C->fn, e->loc, s->name }));
	if (s->kind == SYM_GLOBAL || C->unevaluated) {
		e->use = (ctx == CTX_VALUE && type_is_copy(vt)) ? USE_COPY : USE_BORROW;
		if (ctx == CTX_VALUE && !type_is_copy(vt) && !C->unevaluated) {
			sema_error(e->loc, "can't move '%s' out of a global; it can only be read or borrowed", s->name);
			if (vt->kind == TY_STR)
				sema_note(e->loc, "write '%s.copy' for a string of your own", s->name);
		}
		return;
	}
	switch (ctx) {
	case CTX_VALUE:
		if (type_is_copy(vt)) {
			e->use = USE_COPY;
			record_access(C, s, ACC_READ, e->loc);
		} else if (through) {
			e->use = USE_BORROW;
			sema_error(e->loc, "can't take ownership of '%s' through a borrow", s->name);
			sema_note(s->loc, "'%s' is a borrow ('%s'); the value it points to belongs to someone else",
				  s->name, ts(s->type));
		} else {
			e->use = USE_MOVE;
			record_access(C, s, ACC_MOVE, e->loc);
		}
		break;
	case CTX_READ:
		e->use = USE_BORROW;
		record_access(C, s, ACC_READ, e->loc);
		break;
	case CTX_PLACE:
		e->use = USE_BORROW;
		note_use(C, s, e->loc);
		break;
	}
}

static Type *check_ident(Checker *C, Expr **ep, Ctx ctx, Type *expected)
{
	Expr *e = *ep;
	const char *n = e->as.ident.name;
	Symbol *s = lookup_local(C, n);
	if (!s && C->self_obj) {
		FieldInfo *f = lookup_field(C->self_obj, n);
		if (f) {
			/* bare field name inside a method: self.name */
			Expr *self = ast_expr(C->A, EXPR_SELF, e->loc);
			Expr *fe = ast_expr(C->A, EXPR_FIELD, e->loc);
			fe->as.field.object = self;
			fe->as.field.name = n;
			*ep = fe;
			return check_expr(C, ep, ctx, expected);
		}
		if (lookup_method(C->self_obj, n)) {
			/* a method's name on its own calls it: `area * 2` */
			Expr *call = ast_expr(C->A, EXPR_CALL, e->loc);
			call->as.call.callee = e;
			call->as.call.slot = -1;
			call->as.call.implicit = 2;
			*ep = call;
			return check_expr_inner(C, ep, ctx, expected);
		}
	}
	if (!s) {
		GlobalName *g = find_global(C, n, e->loc);
		if (g && g->kind == GN_GLOBAL) {
			s = g->sym;
		} else if (g && g->kind == GN_ENUM_CONST) {
			e->kind = EXPR_INT;
			memset(&e->as, 0, sizeof e->as);
			e->as.int_lit.value = (uint64_t)g->value;
			return g->type;
		} else if (g && g->kind == GN_FN) {
			/* a function's name on its own calls it: there are no function values */
			Expr *call = ast_expr(C->A, EXPR_CALL, e->loc);
			call->as.call.callee = e;
			call->as.call.slot = -1;
			call->as.call.implicit = 1;
			*ep = call;
			return check_expr_inner(C, ep, ctx, expected);
		} else if (find_type(C, n)) {
			sema_error(e->loc, "'%s' is a type, not a value", n);
			return &t_error;
		} else if (find_builtin(n) == BUILTIN_READ_LINE || find_builtin(n) == BUILTIN_END_OF_INPUT ||
			   find_builtin(n) == BUILTIN_READ_CHAR || find_builtin(n) == BUILTIN_READ_KEY ||
			   find_builtin(n) == BUILTIN_DIRS_CURRENT || find_builtin(n) == BUILTIN_DIRS_HOME ||
			   find_builtin(n) == BUILTIN_ADDRESSES_NONE) {
			/* `str Line = read_line`: a name alone calls it */
			Expr *call = ast_expr(C->A, EXPR_CALL, e->loc);
			call->as.call.callee = e;
			call->as.call.slot = -1;
			call->as.call.implicit = 1;
			*ep = call;
			return check_expr_inner(C, ep, ctx, expected);
		} else if (find_builtin(n) == BUILTIN_TO_STDOUT || find_builtin(n) == BUILTIN_TO_STDERR) {
			sema_error(e->loc, "'%s' goes in front of a call, to send what the call prints to %s: "
				   "'%s println \"...\"'", n, n, n);
			return &t_error;
		} else if (find_builtin(n)) {
			sema_error(e->loc, "'%s' is a built-in function; call it as '%s ...'", n, n);
			return &t_error;
		} else {
			sema_error(e->loc, "unknown name '%s'%s", n, name_hint(n));
			return &t_error;
		}
	}
	e->as.ident.sym = s;
	ident_use(C, e, s, ctx);
	return s->type->kind == TY_REF ? s->type->inner : s->type;
}

/* ====================================================== calls */

static bool derives_from(ObjectInfo *o, const char *name)
{
	for (int i = 0; i < o->mro_len; i++)
		if (strcmp(o->mro[i]->name, name) == 0)
			return true;
	return false;
}

/* Is `e` the object a method was called on, or a part of it that stays
 * inside it (a field, or an element of a fixed array field)? A handler given
 * to such a part can never outlive the object whose method it calls. */
static bool part_of_self(const Expr *e)
{
	switch (e->kind) {
	case EXPR_SELF:
		return true;
	case EXPR_FIELD:
		return part_of_self(e->as.field.object);
	case EXPR_INDEX:
		return e->as.index.base->ty && e->as.index.base->ty->kind == TY_ARRAY && part_of_self(e->as.index.base);
	default:
		return false;
	}
}

static int param_index(FnDecl *fn, const char *name)
{
	for (int i = 0; fn && i < fn->params.len; i++)
		if (strcmp(fn->params.items[i]->name, name) == 0)
			return i;
	return -1;
}

/* An argument for a `handler` parameter: the name of one of this object's
 * methods (bound to the object), or a handler parameter passed on. Where it
 * goes is recorded, and checked once every body is known. */
/* A function of the program's (or C's) passed where C wants a function
 * pointer: its name, and its shape the callback's */
static void check_callback_arg(Checker *C, Expr **ap, Type *pt, const char *what)
{
	Expr *a = *ap;
	FnDecl *sig = pt->sig;
	const char *n = a->kind == EXPR_IDENT ? a->as.ident.name : NULL;
	GlobalName *g = n && !lookup_local(C, n) ? find_global(C, n, a->loc) : NULL;
	if (!g || g->kind != GN_FN) {
		check_expr(C, ap, CTX_READ, NULL);
		sema_error(a->loc, "expected the name of a function for %s (a '%s': '%s %s ...')", what, pt->name,
			   ts(sig->ret_type ? sig->ret_type : &t_void), pt->name);
		return;
	}
	FnDecl *f = g->fn;
	bool same = f->params.len == sig->params.len && f->ret_type && sig->ret_type && type_eq(f->ret_type, sig->ret_type);
	for (int k = 0; same && k < f->params.len; k++)
		same = f->params.items[k]->type->resolved && sig->params.items[k]->type->resolved &&
		       type_eq(f->params.items[k]->type->resolved, sig->params.items[k]->type->resolved);
	if (!same) {
		char want[256] = "", got[256] = "";
		size_t wn = 0, gn = 0;
		for (int k = 0; k < sig->params.len; k++)
			wn += (size_t)snprintf(want + wn, sizeof want - (wn < sizeof want ? wn : sizeof want), "%s%s",
					       k ? ", " : "", ts(sig->params.items[k]->type->resolved));
		for (int k = 0; k < f->params.len; k++)
			gn += (size_t)snprintf(got + gn, sizeof got - (gn < sizeof got ? gn : sizeof got), "%s%s",
					       k ? ", " : "", f->params.items[k]->type->resolved ?
					       ts(f->params.items[k]->type->resolved) : "?");
		sema_error(a->loc, "'%s' isn't the shape of a '%s': it takes (%s) and gives '%s', where a '%s' takes "
			   "(%s) and gives '%s'", n, pt->name, got, f->ret_type ? ts(f->ret_type) : "?", pt->name, want,
			   ts(sig->ret_type ? sig->ret_type : &t_void));
		a->ty = &t_error;
		return;
	}
	memset(&a->as, 0, sizeof a->as);
	a->kind = EXPR_CALLBACK;
	a->as.callback.fn = f;
	a->ty = pt;
}

static void check_handler_arg(Checker *C, Expr **ap, const char *what, FnDecl *to, int to_param, Expr *recv)
{
	Expr *a = *ap;
	HandlerFlow fl = { .loc = a->loc, .to = to, .to_param = to_param, .from_param = -1,
			   .part_of_self = recv && part_of_self(recv) };
	if (a->kind == EXPR_IDENT) {
		const char *n = a->as.ident.name;
		Symbol *s = lookup_local(C, n);
		if (s) {
			Type *t = check_expr(C, ap, CTX_VALUE, NULL);
			if (!is_err(t) && t->kind != TY_HANDLER)
				sema_error(a->loc, "expected a handler for %s: the name of one of this object's methods", what);
			else if (!is_err(t) && s->kind == SYM_PARAM) {
				fl.from = C->fn;
				fl.from_param = param_index(C->fn, n);
				vec_push(C->A, C->flows, fl);
			}
			return;
		}
		FnDecl *m = C->self_obj ? lookup_method(C->self_obj, n) : NULL;
		if (m) {
			if (m->self_kind == SELF_NONE)
				sema_error(a->loc, "'%s' is a static method; a handler needs the object it belongs to", n);
			else if (m->params.len)
				sema_error(a->loc, "'%s' takes arguments; a handler takes none", n);
			else if (m->ret_type && m->ret_type->kind != TY_VOID)
				sema_error(a->loc, "'%s' returns a value; a handler returns nothing ('void')", n);
			else if (C->fn->is_destructor)
				sema_error(a->loc, "a destructor can't hand out its object's methods: the object is going");
			else {
				memset(&a->as, 0, sizeof a->as);
				a->kind = EXPR_HANDLER;
				a->as.handler.method = m;
				a->as.handler.slot = m->vtable_slot;
				a->as.handler.self = C->fn->self_sym;
				a->ty = &t_handler;
				fl.window_target = derives_from(C->self_obj, "gui.window") || derives_from(C->self_obj, "tui.window") ||
						   derives_from(C->self_obj, "game.window") || derives_from(C->self_obj, "gtk.window") ||
					   derives_from(C->self_obj, "tgame.window") || derives_from(C->self_obj, "world.window");
				fl.name = n;
				vec_push(C->A, C->flows, fl);
				return;
			}
			a->ty = &t_error;
			return;
		}
	}
	sema_error(a->loc, "%s is a handler: give the name of one of this object's methods, e.g. 'ok.on_click save'",
		   what);
	check_expr(C, ap, CTX_READ, NULL);
}

/* What happens to a function's handler parameter, from what its body does
 * with it: kept in a field, or given to C. Methods that override one
 * another count together, since a call may reach any of them. */
static unsigned handler_bits(Checker *C, FnDecl *fn, bool native)
{
	unsigned bits = native ? fn->handler_native : fn->handler_keeps;
	if (fn->owner)
		for (int i = 0; i < C->S->nobjects; i++) {
			ObjectInfo *o = C->S->objects[i];
			for (int j = 0; o->kind == OBJ_OBJECT && j < o->decl->methods.len; j++) {
				FnDecl *m = o->decl->methods.items[j];
				if (m != fn && strcmp(m->name, fn->name) == 0 && m->params.len == fn->params.len)
					bits |= native ? m->handler_native : m->handler_keeps;
			}
		}
	return bits;
}

/* Library headers may give C their own object's methods: their objects
 * take them back in their destructors (processes.hby does). */
static bool from_header(SrcLoc l)
{
	const char *n = diag_file_name(l.file);
	size_t k = n ? strlen(n) : 0;
	return k > 4 && strcmp(n + k - 4, ".hby") == 0;
}

void check_handler_flows(Checker *C)
{
	/* a parameter passed on does whatever the parameter it goes to does */
	for (bool changed = true; changed;) {
		changed = false;
		for (int i = 0; i < C->flows.len; i++) {
			HandlerFlow *f = &C->flows.items[i];
			if (!f->from || f->from_param < 0)
				continue;
			unsigned bit = 1u << f->from_param;
			if (f->to->is_extern && !(f->from->handler_native & bit)) {
				f->from->handler_native |= bit;
				changed = true;
			}
			if (f->to->is_extern)
				continue;
			unsigned kb = handler_bits(C, f->to, false) >> f->to_param & 1;
			unsigned nb = handler_bits(C, f->to, true) >> f->to_param & 1;
			if (kb && !(f->from->handler_keeps & bit)) {
				f->from->handler_keeps |= bit;
				changed = true;
			}
			if (nb && !(f->from->handler_native & bit)) {
				f->from->handler_native |= bit;
				changed = true;
			}
		}
	}
	for (int i = 0; i < C->flows.len; i++) {
		HandlerFlow *f = &C->flows.items[i];
		bool keeps = !f->to->is_extern && (handler_bits(C, f->to, false) >> f->to_param & 1);
		bool native = f->to->is_extern || (handler_bits(C, f->to, true) >> f->to_param & 1);
		if (keeps && !f->part_of_self)
			sema_error(f->loc, "'%s' keeps this handler, so it can only be given to this object or a part of "
				   "it (one of its fields): then it can never outlive the object whose method it calls",
				   f->to->name);
		else if (native && f->from_param < 0 && !f->window_target && !from_header(f->loc))
			sema_error(f->loc, "'%s' gives this handler to a window's widgets, so it must be a method of a "
				   "window (an object made from 'gui.window', 'tui.window', 'game.window' or 'world.window'), which lives as long as "
				   "they do", f->to->name);
	}
}

/* `Tick = h`: a handler parameter kept in a field of this object (or of a
 * part of it), to be called later with `Tick.call`. */
static Type *check_handler_store(Checker *C, Expr *e, bool compound)
{
	Expr *target = e->as.assign.target;
	Expr **vp = &e->as.assign.value;
	Expr *v = *vp;
	if (compound) {
		sema_error(e->loc, "a handler can only be set with '='");
		return &t_void;
	}
	Symbol *s = v->kind == EXPR_IDENT ? lookup_local(C, v->as.ident.name) : NULL;
	if (!s || s->kind != SYM_PARAM || s->type->kind != TY_HANDLER) {
		sema_error(v->loc, "a handler field is set from a handler parameter (e.g. 'void on_tick handler h { "
			   "Tick = h }'), and cleared with '.clear'");
		check_expr(C, vp, CTX_READ, NULL);
		return &t_void;
	}
	if (!part_of_self(target)) {
		sema_error(target->loc, "a handler can only be kept in this object's own fields (or a part of it), "
			   "so it can never outlive the object whose method it calls");
		return &t_void;
	}
	if (C->fn->is_destructor) {
		sema_error(e->loc, "a destructor can't keep a handler: its object is going");
		return &t_void;
	}
	check_expr(C, vp, CTX_VALUE, NULL);
	int pi = param_index(C->fn, v->as.ident.name);
	if (pi >= 0)
		C->fn->handler_keeps |= 1u << pi;
	Expr *t = e->as.assign.target;
	make_builtin(C, e, BUILTIN_HANDLER_SET, "=", t, *vp);
	e->ty = &t_void;
	return &t_void;
}

/* stands for the receiver of a method called by its bare name */
static Expr self_expr = { .kind = EXPR_SELF };

static Type *check_args(Checker *C, Expr *call, FnDecl *fn, const char *fname)
{
	ExprVec *args = &call->as.call.args;
	int n = fn->params.len;
	if (args->len < n || (args->len > n && !fn->is_variadic)) {
		sema_error(call->loc, "'%s' takes %d argument%s but %d %s given", fname, n, n == 1 ? "" : "s",
			   args->len, args->len == 1 ? "was" : "were");
		sema_note(fn->loc, "'%s' is declared here", fname);
	}
	for (int i = 0; i < args->len; i++) {
		Expr **ap = &args->items[i];
		char what[160];
		snprintf(what, sizeof what, "argument %d of '%s'", i + 1, fname);
		if (i >= n) {
			if (!fn->is_variadic) {
				check_expr(C, ap, CTX_READ, NULL);
				continue;
			}
			/* C variadic: default argument promotions */
			Type *t = check_expr(C, ap, CTX_READ, NULL);
			if (is_err(t))
				continue;
			if (t->kind == TY_FLOAT && t->bits == 32)
				convert_to(C, ap, &t_double);
			else if (t->kind == TY_BOOL || (t->kind == TY_INT && t->bits < 32))
				convert_to(C, ap, &t_int);
			else if (t->kind != TY_INT && t->kind != TY_FLOAT && t->kind != TY_STR)
				sema_error((*ap)->loc, "can't pass '%s' to the '...' part of '%s'", ts(t), fname);
			continue;
		}
		VarDecl *p = fn->params.items[i];
		Type *pt = p->type->resolved ? p->type->resolved : &t_error;
		if (is_err(pt)) {
			check_expr(C, ap, CTX_READ, NULL);
			continue;
		}
		if (pt->kind == TY_CALLBACK) {
			check_callback_arg(C, ap, pt, what);
		} else if (pt->kind == TY_HANDLER) {
			Expr *callee = call->as.call.callee;
			Expr *recv = callee->kind == EXPR_FIELD ? callee->as.field.object
				     : fn->owner && fn->self_kind != SELF_NONE ? &self_expr : NULL;
			check_handler_arg(C, ap, what, fn, i, recv);
		} else if (pt->kind == TY_REF || pt->kind == TY_SLICE) {
			g_ref_prechecked = i < 32 && (call->as.call.checked_args >> i & 1);
			bind_ref(C, ap, pt, NULL, what);
		} else if (p->is_extern_str) {
			Type *t = check_expr(C, ap, CTX_READ, NULL);
			if (!is_err(t) && t->kind != TY_STR)
				sema_error((*ap)->loc, "expected 'str' for %s, found '%s'", what, ts(t));
		} else {
			if (!(i < 32 && (call->as.call.checked_args >> i & 1)))
				check_expr(C, ap, CTX_VALUE, pt);
			coerce(C, ap, pt, what);
		}
	}
	return fn->ret_type ? fn->ret_type : &t_void;
}

static FnDecl *type_method(Checker *C, const Type *t, const char *name, SrcLoc l);
static Type *check_method_call_on(Checker *C, Expr *call, bool recv_checked);

/* An object (or a borrow of one) with a method of this name, or NULL */
static ObjectInfo *object_with(const Type *t, const char *name)
{
	while (t && t->kind == TY_REF)
		t = t->inner;
	if (!t || t->kind != TY_OBJECT || t->obj->kind != OBJ_OBJECT || !lookup_method(t->obj, name))
		return NULL;
	return t->obj;
}

/* e becomes recv.name(a1, a2): recv checked already, the arguments not */
static Type *call_object_method(Checker *C, Expr *e, Expr *recv, const char *name, Expr *a1, Expr *a2)
{
	SrcLoc l = e->loc;
	Expr *callee = ast_expr(C->A, EXPR_FIELD, l);
	callee->as.field.object = recv;
	callee->as.field.name = name;
	memset(&e->as, 0, sizeof e->as);
	e->kind = EXPR_CALL;
	e->as.call.callee = callee;
	e->as.call.slot = -1;
	if (a1)
		vec_push(C->A, e->as.call.args, a1);
	if (a2)
		vec_push(C->A, e->as.call.args, a2);
	return check_method_call_on(C, e, true);
}
static Type *call_type_method(Checker *C, Expr *e, Expr *recv, FnDecl *fn, const char *name);

/* What print and println can show: numbers, bools, chars, strings, and
 * lists of those (as [1, 2, 3]). */
static bool printable(const Type *t)
{
	if (type_is_arith(t) || t->kind == TY_STR)
		return true;
	return (t->kind == TY_LIST || t->kind == TY_LOOKUP) && printable(t->inner);
}

/* files.*, directories.* and shell.*: every argument is a str (a path, text,
 * a command or a name) */
static Type *check_lib_builtin(Checker *C, Expr *call, Builtin b)
{
	static const struct { Builtin b; int nargs; char ret; const char *example; } sig[] = {
		{ BUILTIN_FILES_READ, 1, 's', "files.read \"notes.txt\"" },
		{ BUILTIN_FILES_LINES, 1, 'L', "files.lines \"notes.txt\"" },
		{ BUILTIN_FILES_WRITE, 2, 'v', "files.write \"out.txt\", Text" },
		{ BUILTIN_FILES_APPEND, 2, 'v', "files.append \"log.txt\", Line" },
		{ BUILTIN_FILES_EXISTS, 1, 'b', "files.exists \"notes.txt\"" },
		{ BUILTIN_FILES_SIZE, 1, 'l', "files.size \"notes.txt\"" },
		{ BUILTIN_FILES_COPY, 2, 'v', "files.copy \"a.txt\", \"b.txt\"" },
		{ BUILTIN_FILES_RENAME, 2, 'v', "files.rename \"a.txt\", \"b.txt\"" },
		{ BUILTIN_FILES_DELETE, 1, 'v', "files.delete \"old.txt\"" },
		{ BUILTIN_DIRS_LIST, 1, 'L', "directories.list \"photos\"" },
		{ BUILTIN_DIRS_ALL_FILES, 1, 'L', "directories.all_files \"photos\"" },
		{ BUILTIN_DIRS_EXISTS, 1, 'b', "directories.exists \"photos\"" },
		{ BUILTIN_DIRS_MAKE, 1, 'v', "directories.make \"backup\"" },
		{ BUILTIN_DIRS_REMOVE, 1, 'v', "directories.remove \"backup\"" },
		{ BUILTIN_DIRS_RENAME, 2, 'v', "directories.rename \"old\", \"new\"" },
		{ BUILTIN_DIRS_CURRENT, 0, 's', "directories.current" },
		{ BUILTIN_DIRS_CHANGE_TO, 1, 'v', "directories.change_to \"photos\"" },
		{ BUILTIN_DIRS_HOME, 0, 's', "directories.home" },
		{ BUILTIN_DIRS_SAVE_FOLDER, 1, 's', "directories.save_folder \"My Game\"" },
		{ BUILTIN_FILES_OPEN, 1, 'f', "files.open \"data.txt\"" },
		{ BUILTIN_FILES_OPEN_TO_WRITE, 1, 'f', "files.open_to_write \"out.txt\"" },
		{ BUILTIN_FILES_OPEN_TO_ADD, 1, 'f', "files.open_to_add \"log.txt\"" },
		{ BUILTIN_FILES_OPEN_TO_CHANGE, 1, 'f', "files.open_to_change \"save.dat\"" },
		{ BUILTIN_SHELL_RUN, 1, 's', "shell.run \"date\"" },
		{ BUILTIN_SHELL_STATUS, 1, 'i', "shell.status \"make\"" },
		{ BUILTIN_SHELL_GET, 1, 's', "shell.get \"HOME\"" },
		{ BUILTIN_SHELL_HAS, 1, 'b', "shell.has \"EDITOR\"" },
		{ BUILTIN_SHELL_SET, 2, 'v', "shell.set \"LANG\", \"C\"" },
		{ BUILTIN_SHELL_UNSET, 1, 'v', "shell.unset \"LANG\"" },
	};
	size_t k = 0;
	while (sig[k].b != b)
		k++;
	const char *name = call->as.call.callee->as.ident.name;
	int n = call->as.call.args.len;
	if (n != sig[k].nargs) {
		if (sig[k].nargs == 0)
			sema_error(call->loc, "'%s' takes no arguments", name);
		else
			sema_error(call->loc, "'%s' takes %d argument%s, but %d %s given (e.g. '%s')", name, sig[k].nargs,
				   sig[k].nargs == 1 ? "" : "s", n, n == 1 ? "was" : "were", sig[k].example);
		for (int i = 0; i < n; i++)
			check_expr(C, &call->as.call.args.items[i], CTX_READ, NULL);
		return &t_error;
	}
	bool ok = true;
	for (int i = 0; i < n; i++) {
		Expr **ap = &call->as.call.args.items[i];
		Type *t = check_expr(C, ap, CTX_READ, NULL);
		if (is_err(t)) {
			ok = false;
		} else if (t->kind != TY_STR) {
			bool text = (b == BUILTIN_FILES_WRITE || b == BUILTIN_FILES_APPEND) && i == 1;
			const char *what = text ? "the text" : b == BUILTIN_SHELL_RUN || b == BUILTIN_SHELL_STATUS ? "the command"
					   : b == BUILTIN_SHELL_SET && i == 1 ? "the value"
					   : b >= BUILTIN_SHELL_GET && b <= BUILTIN_SHELL_UNSET ? "the name" : "a path";
			sema_error((*ap)->loc, "'%s' takes %s as a 'str', not '%s'%s", name, what,
				   ts(t), type_is_arith(t) ? " (use .text to turn a number into text)" : "");
			ok = false;
		} else if (is_place_expr(*ap)) {
			borrow_place(C, *ap, false, (*ap)->loc, NULL);
		}
	}
	if (!ok)
		return &t_error;
	switch (sig[k].ret) {
	case 's': return &t_str;
	case 'b': return &t_bool;
	case 'l': return &t_long;
	case 'i': return &t_int;
	case 'L': return type_list(C->A, &t_str);
	case 'f': return &t_file;
	default:  return &t_void;
	}
}

/* addresses.of_text S, addresses.of_items L: the characters of a string, or
 * the items of a list, as C keeps them */
static Type *check_of_text_items(Checker *C, Expr *call, Builtin b)
{
	ExprVec *args = &call->as.call.args;
	bool text = b == BUILTIN_ADDRESSES_OF_TEXT;
	if (args->len != 1) {
		sema_error(call->loc, "'addresses.%s' takes one argument (e.g. 'addresses.%s %s')", text ? "of_text" : "of_items",
			   text ? "of_text" : "of_items", text ? "Name" : "Scores");
		for (int i = 0; i < args->len; i++)
			check_expr(C, &args->items[i], CTX_READ, NULL);
		return &t_error;
	}
	Expr **ap = &args->items[0];
	Type *t = check_expr(C, ap, CTX_PLACE, NULL);
	if (is_err(t))
		return t;
	if (t->kind == TY_REF)
		t = t->inner;
	if (!is_place_expr(*ap)) {
		sema_error((*ap)->loc, "'addresses.%s' needs a variable, field or element that holds the %s, so it stays "
			   "while the address is used", text ? "of_text" : "of_items", text ? "text" : "list");
		return &t_error;
	}
	if (text) {
		if (t->kind != TY_STR) {
			sema_error((*ap)->loc, "'addresses.of_text' takes a str, not '%s'%s", ts(t),
				   t->kind == TY_LIST ? " (for a list: addresses.of_items)" : "");
			return &t_error;
		}
		call->as.call.builtin = b;
		return type_address(C->A, &t_char);
	}
	if (t->kind != TY_LIST) {
		sema_error((*ap)->loc, "'addresses.of_items' takes a list, not '%s'%s", ts(t),
			   t->kind == TY_STR ? " (for a str: addresses.of_text)" : t->kind == TY_ARRAY ? " (for an array: addresses.of)" : "");
		return &t_error;
	}
	if (!type_is_plain(t->inner)) {
		sema_error((*ap)->loc, "an address can only point at plain data (numbers, bool, char, other addresses, "
			   "and structs and arrays of those), not '%s'", ts(t->inner));
		return &t_error;
	}
	call->as.call.builtin = b;
	return type_address(C->A, t->inner);
}

/* addresses.of X, addresses.at N, addresses.none */
static Type *check_addresses_builtin(Checker *C, Expr *call, Builtin b)
{
	ExprVec *args = &call->as.call.args;
	Type *want = C->call_expected;
	int need = b == BUILTIN_ADDRESSES_NONE ? 0 : 1;
	const char *name = b == BUILTIN_ADDRESSES_OF ? "addresses.of" : b == BUILTIN_ADDRESSES_AT ? "addresses.at"
			   : "addresses.none";
	if (args->len != need) {
		sema_error(call->loc, need ? "'%s' takes one argument (e.g. 'addresses.%s %s')" : "'%s' takes no arguments",
			   name, b == BUILTIN_ADDRESSES_OF ? "of" : "at", b == BUILTIN_ADDRESSES_OF ? "Count" : "0x0200");
		for (int i = 0; i < args->len; i++)
			check_expr(C, &args->items[i], CTX_READ, NULL);
		return &t_error;
	}
	if (b == BUILTIN_ADDRESSES_OF) {
		Expr **ap = &args->items[0];
		Type *t = check_expr(C, ap, CTX_PLACE, NULL);
		if (is_err(t))
			return t;
		if (!is_place_expr(*ap)) {
			sema_error((*ap)->loc, "'addresses.of' needs a variable, field or element: something with a "
				   "place in memory");
			return &t_error;
		}
		PlaceInfo pi = analyze_place(*ap);
		if (pi.ok && pi.root->is_register) {
			sema_error((*ap)->loc, "'%s' is register: it has no address", pi.root->name);
			return &t_error;
		}
		if (pi.ok && pi.root->is_loop_var) {
			sema_error((*ap)->loc, "'%s' is a loop's counter: it has no address of its own", pi.root->name);
			return &t_error;
		}
		Type *el = t->kind == TY_ARRAY ? t->inner : t;   /* an array: its first element, as in C */
		if (t->kind == TY_REF)
			el = t->inner;
		if (!type_is_plain(el)) {
			sema_error((*ap)->loc, "an address can only point at plain data (numbers, bool, char, other addresses, "
				   "and structs and arrays of those), not '%s'", ts(el));
			return &t_error;
		}
		call->as.call.builtin = b;
		return type_address(C->A, el);
	}
	if (!want || want->kind != TY_ADDRESS) {
		sema_error(call->loc, "say what kind of address this is, e.g. 'address short Port = %s%s'", name,
			   b == BUILTIN_ADDRESSES_AT ? " 0x0200" : "");
		for (int i = 0; i < args->len; i++)
			check_expr(C, &args->items[i], CTX_READ, NULL);
		return &t_error;
	}
	if (b == BUILTIN_ADDRESSES_AT) {
		Type *t = check_expr(C, &args->items[0], CTX_VALUE, NULL);
		if (is_err(t))
			return t;
		if (t->kind != TY_INT) {
			sema_error(args->items[0]->loc, "'addresses.at' takes a whole number, not '%s'", ts(t));
			return &t_error;
		}
		convert_to(C, &args->items[0], &t_ulong);
	}
	call->as.call.builtin = b;
	return want;
}

/* addresses.allocate N, addresses.free A, addresses.reallocate A, N */
static Type *check_memory_builtin(Checker *C, Expr *call, Builtin b)
{
	ExprVec *args = &call->as.call.args;
	const char *name = b == BUILTIN_ADDRESSES_ALLOCATE ? "allocate" : b == BUILTIN_ADDRESSES_FREE ? "free" : "reallocate";
	int need = b == BUILTIN_ADDRESSES_REALLOCATE ? 2 : 1;
	if (args->len != need) {
		sema_error(call->loc, "'addresses.%s' takes %s (e.g. '%s')", name, need == 2 ? "two arguments" : "one argument",
			   b == BUILTIN_ADDRESSES_ALLOCATE ? "address int Buffer = addresses.allocate 100"
			   : b == BUILTIN_ADDRESSES_FREE ? "addresses.free Buffer" : "Buffer = addresses.reallocate Buffer, 200");
		for (int i = 0; i < args->len; i++)
			check_expr(C, &args->items[i], CTX_READ, NULL);
		return &t_error;
	}
	Type *at = NULL;
	if (b == BUILTIN_ADDRESSES_ALLOCATE) {
		at = C->call_expected;
		if (!at || at->kind != TY_ADDRESS || !at->inner) {
			sema_error(call->loc, "say what the memory is for, e.g. 'address int Buffer = addresses.allocate 100'");
			check_expr(C, &args->items[0], CTX_READ, NULL);
			return &t_error;
		}
	} else {
		at = check_expr(C, &args->items[0], CTX_VALUE, NULL);
		if (is_err(at))
			return at;
		if (at->kind != TY_ADDRESS) {
			sema_error(args->items[0]->loc, "'addresses.%s' takes an address, not '%s'", name, ts(at));
			return &t_error;
		}
		const Expr *x = args->items[0];
		while (x->kind == EXPR_CAST)
			x = x->as.cast.value;
		if (x->kind == EXPR_CALL && (x->as.call.builtin == BUILTIN_ADDRESSES_OF ||
					     x->as.call.builtin == BUILTIN_ADDRESSES_OF_TEXT ||
					     x->as.call.builtin == BUILTIN_ADDRESSES_OF_ITEMS)) {
			sema_error(args->items[0]->loc, "this is the address of one of Cabby's own values, which gives its "
				   "memory back itself when it's dropped: only memory from addresses.allocate can be %s",
				   b == BUILTIN_ADDRESSES_FREE ? "freed" : "reallocated");
			return &t_error;
		}
		if (b == BUILTIN_ADDRESSES_REALLOCATE && !at->inner) {
			sema_error(args->items[0]->loc, "say what the memory holds (cast it, e.g. (address int)Block), so the "
				   "new size can be worked out");
			return &t_error;
		}
	}
	if (b != BUILTIN_ADDRESSES_FREE) {
		Expr **np = &args->items[need - 1];
		Type *nt = check_expr(C, np, CTX_VALUE, NULL);
		if (is_err(nt))
			return nt;
		if (nt->kind != TY_INT) {
			sema_error((*np)->loc, "how many values: a whole number, not '%s'", ts(nt));
			return &t_error;
		}
		convert_to(C, np, &t_long);
	}
	call->as.call.builtin = b;
	return b == BUILTIN_ADDRESSES_FREE ? &t_void : at;
}

/* A.value and A[i]: what's at an address, as a place */
static Type *make_at(Checker *C, Expr *e, Expr *addr, Expr *step)
{
	Type *at = addr->ty;
	if (!at->inner) {
		sema_error(e->loc, "an 'address' of nothing in particular has nothing to read: say what's there, "
			   "e.g. 'address int'");
		return &t_error;
	}
	memset(&e->as, 0, sizeof e->as);
	e->kind = EXPR_AT;
	e->as.at.address = addr;
	e->as.at.step = step;
	(void)C;
	return at->inner;
}

/* A + n, A - n, A - B, and comparing two addresses */
static Type *check_address_binary(Checker *C, Expr *e, BinaryOp op, Expr **lp, Expr **rp, Type *lt, Type *rt)
{
	const char *o = ast_binop_str(op);
	bool la = lt->kind == TY_ADDRESS, ra = rt->kind == TY_ADDRESS;
	if (la && ra) {
		if (!type_eq(lt, rt)) {
			sema_error(e->loc, "'%s' needs two addresses of the same kind, not '%s' and '%s'", o, ts(lt), ts(rt));
			return &t_error;
		}
		if (ast_binop_is_comparison(op))
			return &t_bool;
		if (op == BIN_SUB) {
			if (!lt->inner) {
				sema_error(e->loc, "addresses of nothing in particular can't be counted apart");
				return &t_error;
			}
			make_builtin(C, e, BUILTIN_ADDRESS_DIFF, "-", *lp, *rp);
			return &t_long;
		}
	} else if ((op == BIN_ADD || op == BIN_SUB) && (la ? type_is_arith(rt) && rt->kind == TY_INT
						      : op == BIN_ADD && lt->kind == TY_INT)) {
		Type *at = la ? lt : rt;
		if (!at->inner) {
			sema_error(e->loc, "an 'address' of nothing in particular can't be stepped: say what's there, "
				   "e.g. 'address int'");
			return &t_error;
		}
		if (!la) {   /* n + A: the address first */
			Expr *t = *lp;
			*lp = *rp;
			*rp = t;
		}
		convert_to(C, rp, &t_long);
		return at;
	}
	sema_error(e->loc, "'%s' can't be used with '%s' and '%s' (addresses step with + and -, and compare)", o,
		   ts(lt), ts(rt));
	return &t_error;
}

/* close_to A, B (, Limit): numbers of any kind, made doubles; whether A
 * and B were both floats picks the default limits (call->as.call.flag) */
static Type *check_close_to(Checker *C, Expr *call)
{
	ExprVec *args = &call->as.call.args;
	Type *ts_[3] = { NULL, NULL, NULL };
	for (int i = 0; i < args->len; i++)
		ts_[i < 3 ? i : 2] = check_expr(C, &args->items[i], CTX_VALUE, NULL);
	if (args->len != 2 && args->len != 3) {
		sema_error(call->loc, "'close_to' takes two numbers, and a limit if you like: 'close_to A, B' or "
			   "'close_to A, B, 0.001'");
		return &t_error;
	}
	for (int i = 0; i < args->len; i++) {
		if (is_err(ts_[i]))
			return &t_error;
		if (!type_is_arith(ts_[i]) || ts_[i]->kind == TY_BOOL) {
			sema_error(args->items[i]->loc, "'close_to' takes numbers, not '%s'", ts(ts_[i]));
			return &t_error;
		}
	}
	bool floats = ts_[0]->kind == TY_FLOAT && ts_[0]->bits == 32 && ts_[1]->kind == TY_FLOAT && ts_[1]->bits == 32;
	call->as.call.close_floats = floats;
	for (int i = 0; i < args->len; i++)
		convert_to(C, &args->items[i], &t_double);
	return &t_bool;
}

/* abs, random, random_seed and exit: Cabby's own, for any number */
static Type *check_number_builtin(Checker *C, Expr *call, Builtin b)
{
	ExprVec *args = &call->as.call.args;
	const char *name = b == BUILTIN_ABS ? "abs" : b == BUILTIN_RANDOM ? "random" : b == BUILTIN_RANDOM_SEED ? "random_seed" : "exit";
	int want = b == BUILTIN_RANDOM ? 2 : 1;
	if (b == BUILTIN_EXIT && args->len == 0)
		return &t_void;   /* exit alone: status 0 */
	Type *ts_[2] = { NULL, NULL };
	for (int i = 0; i < args->len; i++)
		ts_[i < 2 ? i : 1] = check_expr(C, &args->items[i], CTX_VALUE, NULL);
	if (args->len != want) {
		if (b == BUILTIN_RANDOM)
			sema_error(call->loc, "random needs a range: 'random 1 to 6' (a whole number from 1 to 6), or "
				   "'random 0.0 to 1.0' (a double from 0 up to 1)");
		else
			sema_error(call->loc, "'%s' takes one %s: '%s %s'", name, b == BUILTIN_ABS ? "number" : "whole number",
				   name, b == BUILTIN_ABS ? "-7" : b == BUILTIN_EXIT ? "1" : "42");
		return &t_error;
	}
	for (int i = 0; i < want; i++) {
		if (is_err(ts_[i]))
			return &t_error;
		bool ok = b == BUILTIN_ABS || b == BUILTIN_RANDOM ? type_is_arith(ts_[i]) && ts_[i]->kind != TY_BOOL
								  : ts_[i]->kind == TY_INT;
		if (!ok) {
			sema_error(args->items[i]->loc, "'%s' takes %s, not '%s'", name,
				   b == BUILTIN_ABS || b == BUILTIN_RANDOM ? "numbers" : "a whole number", ts(ts_[i]));
			return &t_error;
		}
	}
	if (b == BUILTIN_ABS) {
		Type *t = type_promote(ts_[0]);
		convert_to(C, &args->items[0], t);
		return t;
	}
	if (b == BUILTIN_RANDOM) {
		Type *t = type_common(ts_[0], ts_[1]);
		if (t->kind == TY_FLOAT)
			t = &t_double;
		else if (t->bits < 64 || !t->is_signed)
			t = t->bits < 64 && t->is_signed ? &t_int : &t_long;
		convert_to(C, &args->items[0], t);
		convert_to(C, &args->items[1], t);
		return t;
	}
	convert_to(C, &args->items[0], b == BUILTIN_EXIT ? &t_int : &t_long);
	return &t_void;
}

static Type *check_builtin(Checker *C, Expr *call, Builtin b)
{
	call->as.call.builtin = b;
	if (b >= BUILTIN_ADDRESSES_OF && b <= BUILTIN_ADDRESSES_NONE)
		return check_addresses_builtin(C, call, b);
	if (b == BUILTIN_ADDRESSES_OF_TEXT || b == BUILTIN_ADDRESSES_OF_ITEMS)
		return check_of_text_items(C, call, b);
	if (b >= BUILTIN_ADDRESSES_ALLOCATE && b <= BUILTIN_ADDRESSES_REALLOCATE)
		return check_memory_builtin(C, call, b);
	if (b == BUILTIN_EVENTS_RUN || b == BUILTIN_EVENTS_STOP) {
		if (call->as.call.args.len) {
			sema_error(call->loc, "'events.%s' takes no arguments", b == BUILTIN_EVENTS_RUN ? "run" : "stop");
			return &t_error;
		}
		return &t_void;
	}
	if (b >= BUILTIN_FILES_READ && b <= BUILTIN_SHELL_UNSET)
		return check_lib_builtin(C, call, b);
	if (b == BUILTIN_RECOVERED)
		return &t_str;
	if (b == BUILTIN_ABS || b == BUILTIN_RANDOM || b == BUILTIN_RANDOM_SEED || b == BUILTIN_EXIT)
		return check_number_builtin(C, call, b);
	if (b == BUILTIN_CLOSE_TO)
		return check_close_to(C, call);
	if (b == BUILTIN_READ_LINE || b == BUILTIN_END_OF_INPUT || b == BUILTIN_READ_CHAR || b == BUILTIN_READ_KEY) {
		const char *name = b == BUILTIN_READ_LINE ? "read_line" : b == BUILTIN_READ_CHAR ? "read_char"
				   : b == BUILTIN_READ_KEY ? "read_key" : "end_of_input";
		if (call->as.call.args.len) {
			sema_error(call->loc, "'%s' takes no arguments", name);
			for (int i = 0; i < call->as.call.args.len; i++)
				check_expr(C, &call->as.call.args.items[i], CTX_READ, NULL);
		}
		return b == BUILTIN_READ_LINE || b == BUILTIN_READ_KEY ? &t_str : b == BUILTIN_READ_CHAR ? &t_char : &t_bool;
	}
	if (b == BUILTIN_TO_STDOUT || b == BUILTIN_TO_STDERR) {
		const char *name = b == BUILTIN_TO_STDOUT ? "stdout" : "stderr";
		if (call->as.call.args.len != 1) {
			sema_error(call->loc, "put one call after '%s', and everything it prints goes to %s: "
				   "'%s println \"...\"' or '%s report Code'", name, name, name, name);
			for (int i = 0; i < call->as.call.args.len; i++)
				check_expr(C, &call->as.call.args.items[i], CTX_READ, NULL);
			return &t_error;
		}
		Expr **ap = &call->as.call.args.items[0];
		Type *t = check_expr(C, ap, CTX_VALUE, NULL);
		if (is_err(t))
			return t;
		if ((*ap)->kind != EXPR_CALL) {
			sema_error((*ap)->loc, "'%s' sends what a call prints to %s, so a call goes after it "
				   "(a function or method), not a value", name, name);
			return &t_error;
		}
		return t;
	}
	if (b == BUILTIN_INPUT) {
		if (call->as.call.args.len != 1) {
			sema_error(call->loc, "'input' takes 1 argument, the prompt to show (e.g. 'input \"Name: \"'), "
				   "but %d were given", call->as.call.args.len);
			for (int i = 0; i < call->as.call.args.len; i++)
				check_expr(C, &call->as.call.args.items[i], CTX_READ, NULL);
			return &t_str;
		}
		Expr **ap = &call->as.call.args.items[0];
		Type *t = check_expr(C, ap, CTX_READ, NULL);
		if (!is_err(t) && t->kind != TY_STR)
			sema_error((*ap)->loc, "'input' shows a 'str' as its prompt, not '%s'%s", ts(t),
				   type_is_arith(t) ? " (use .text to turn a number into text)" : "");
		else if (!is_err(t) && is_place_expr(*ap))
			borrow_place(C, *ap, false, (*ap)->loc, NULL);
		return &t_str;
	}
	for (int i = 0; i < call->as.call.args.len; i++) {
		Expr **ap = &call->as.call.args.items[i];
		Type *t = check_expr(C, ap, CTX_READ, NULL);
		FnDecl *tf = !is_err(t) ? type_method(C, t, "text", (*ap)->loc) : NULL;
		if (tf) {
			/* a kind or enum with a .text method is shown by it */
			Expr *v = *ap;
			Expr *field = ast_expr(C->A, EXPR_FIELD, v->loc);
			field->as.field.object = v;
			field->as.field.name = "text";
			field->ty = NULL;
			*ap = field;
			t = call_type_method(C, field, v, tf, "text");
			(*ap)->ty = t;
		}
		if (!tf && !is_err(t) && object_with(t, "text")) {
			/* an object with a .text method is shown by it (a json value) */
			Expr *tc = ast_expr(C->A, EXPR_CALL, (*ap)->loc);
			Expr *v = *ap;
			*ap = tc;
			t = call_object_method(C, tc, v, "text", NULL, NULL);
			tc->ty = t;
		}
		if (!is_err(t) && !printable(t))
			sema_error((*ap)->loc, "%s can't show a value of type '%s'",
				   b == BUILTIN_PRINT ? "print" : "println", ts(t));
		else if (!is_err(t) && !type_is_copy(t) && is_place_expr(*ap))
			borrow_place(C, *ap, false, (*ap)->loc, NULL);   /* held until printed */
	}
	return &t_void;
}

/* ====================================================== lists */

/* Elements that .copy (and slices) can duplicate: plain values, strings,
 * and lists of those. Objects can't be copied. */
static bool list_elem_copyable(const Type *t)
{
	if (type_is_copy(t) || t->kind == TY_STR)
		return true;
	return (t->kind == TY_LIST || t->kind == TY_LOOKUP) && list_elem_copyable(t->inner);
}

/* Elements that .contains and .find can compare: numbers, bool, char, str. */
static bool list_elem_comparable(const Type *t)
{
	return type_is_arith(t) || t->kind == TY_STR;
}

/* A list being changed: it must be changeable, and nothing may be
 * borrowing it (or its elements) at the time. */
static bool list_change(Checker *C, Expr *recv, const char *name)
{
	const char *what = recv->ty && recv->ty->kind == TY_LOOKUP ? "lookup" : "list";
	if (!is_place_expr(recv))
		return true;
	PlaceInfo pi = analyze_place(recv);
	if (pi.ok && !pi.mutable) {
		if (pi.root->kind == SYM_SELF)
			sema_error(recv->loc, "'.%s' changes the %s, which a const method can't do", name, what);
		else if (pi.via_ref)
			sema_error(recv->loc, "'.%s' changes the %s, but '%s' is a read-only borrow", name, what, pi.root->name);
		else
			sema_error(recv->loc, "'.%s' changes the %s, but '%s' is const", name, what, pi.root->name);
		explain_immutable(&pi);
		return false;
	}
	borrow_place(C, recv, true, recv->loc, NULL);
	return true;
}

static Type *list_member(Checker *C, Expr *e, Expr *recv, Type *lt, const char *name)
{
	static const struct { const char *name; Builtin b; int nargs; bool changes; } members[] = {
		{ "length", BUILTIN_LIST_LEN, 0, false },
		{ "add", BUILTIN_LIST_ADD, 1, true },
		{ "insert", BUILTIN_LIST_INSERT, 2, true },
		{ "remove_at", BUILTIN_LIST_REMOVE_AT, 1, true },
		{ "take_last", BUILTIN_LIST_TAKE_LAST, 0, true },
		{ "clear", BUILTIN_LIST_CLEAR, 0, true },
		{ "contains", BUILTIN_LIST_CONTAINS, 1, false },
		{ "find", BUILTIN_LIST_FIND, 1, false },
		{ "copy", BUILTIN_LIST_COPY, 0, false },
		{ "join", BUILTIN_LIST_JOIN, 1, false },
		{ "sort", BUILTIN_LIST_SORT, 0, true },
		{ "reverse", BUILTIN_LIST_REVERSE, 0, true },
	};
	static const char *const examples[] = {
		"Scores.length", "Scores.add 90", "Scores.insert 0, 90", "Scores.remove_at 2", "Scores.take_last",
		"Scores.clear", "Scores.contains 90", "Scores.find 90", "Scores.copy", "Words.join \", \"",
		"Scores.sort", "Scores.reverse",
	};
	Type *el = lt->inner;
	for (size_t m = 0; m < sizeof members / sizeof *members; m++) {
		if (strcmp(name, members[m].name) != 0)
			continue;
		ExprVec given = { 0 };
		if (e->kind == EXPR_CALL)
			given = e->as.call.args;
		if (given.len != members[m].nargs) {
			sema_error(e->loc, "'.%s' takes %d argument%s, but %d %s given (e.g. '%s')", name, members[m].nargs,
				   members[m].nargs == 1 ? "" : "s", given.len, given.len == 1 ? "was" : "were", examples[m]);
			for (int k = 0; k < given.len; k++)
				check_expr(C, &given.items[k], CTX_READ, NULL);
			return &t_error;
		}
		Builtin b = members[m].b;
		bool ok = true;
		if (b == BUILTIN_LIST_JOIN && el->kind != TY_STR) {
			sema_error(e->loc, "'.join' joins a list of strings (list str), not '%s'", ts(lt));
			ok = false;
		}
		if ((b == BUILTIN_LIST_CONTAINS || b == BUILTIN_LIST_FIND) && !list_elem_comparable(el)) {
			sema_error(e->loc, "'.%s' compares elements with ==, so it works on lists of numbers, "
				   "bools, chars and strings, not '%s'", name, ts(lt));
			ok = false;
		}
		if (b == BUILTIN_LIST_SORT && !list_elem_comparable(el)) {
			sema_error(e->loc, "'.sort' puts numbers, bools, chars and strings in order, not the elements "
				   "of a '%s'", ts(lt));
			ok = false;
		}
		if (b == BUILTIN_LIST_COPY && !list_elem_copyable(el)) {
			sema_error(e->loc, "a '%s' can't be copied: its elements can't be (objects are never "
				   "copied, only moved)", ts(lt));
			ok = false;
		}
		if (members[m].changes && !list_change(C, recv, name))
			ok = false;
		make_builtin(C, e, b, name, recv, NULL);
		for (int k = 0; k < given.len; k++) {
			vec_push(C->A, e->as.call.args, given.items[k]);
			Expr **ap = &e->as.call.args.items[k + 1];
			bool is_index = (b == BUILTIN_LIST_INSERT && k == 0) || b == BUILTIN_LIST_REMOVE_AT;
			if (is_index) {
				Type *it = check_expr(C, ap, CTX_VALUE, NULL);
				if (is_err(it))
					ok = false;
				else if (it->kind != TY_INT) {
					sema_error((*ap)->loc, "a position in a list must be a whole number, not '%s'", ts(it));
					ok = false;
				} else
					convert_to(C, ap, &t_long);
			} else if (b == BUILTIN_LIST_JOIN) {
				Type *t = check_expr(C, ap, CTX_READ, NULL);
				if (!is_err(t) && t->kind != TY_STR && !(t->kind == TY_INT && t->is_char)) {
					sema_error((*ap)->loc, "'.join' puts text between the strings (a 'str' or a 'char'), "
						   "not '%s'", ts(t));
					ok = false;
				}
			} else if (b == BUILTIN_LIST_CONTAINS || b == BUILTIN_LIST_FIND) {
				Type *t = check_expr(C, ap, CTX_READ, el);
				if (is_err(t))
					ok = false;
				else if (el->kind == TY_STR ? t->kind != TY_STR : !type_is_arith(t)) {
					sema_error((*ap)->loc, "'.%s' on a '%s' looks for a '%s', not '%s'", name, ts(lt),
						   ts(el), ts(t));
					ok = false;
				} else if (type_is_arith(el))
					coerce(C, ap, el, "the value to look for");
			} else {
				/* .add x, .insert i, x: the element is moved into the list */
				char what[64];
				snprintf(what, sizeof what, "an element of '%s'", ts(lt));
				check_expr(C, ap, CTX_VALUE, el);
				if (!coerce(C, ap, el, what))
					ok = false;
			}
		}
		if (!ok)
			return &t_error;
		switch (b) {
		case BUILTIN_LIST_LEN: case BUILTIN_LIST_FIND: return &t_long;
		case BUILTIN_LIST_CONTAINS: return &t_bool;
		case BUILTIN_LIST_TAKE_LAST: return el;
		case BUILTIN_LIST_COPY: return lt;
		case BUILTIN_LIST_JOIN: return &t_str;
		default: return &t_void;
		}
	}
	sema_error(e->loc, "a list has no '.%s' (it has .length, .add, .insert, .remove_at, .take_last, .clear, "
		   ".contains, .find, .copy, .sort, .reverse%s)", name, el->kind == TY_STR ? " and .join" : "");
	return &t_error;
}

/* ====================================================== lookups */

/* A key for lookup type `lt`, checked and converted in place: it is only
 * read (a new key is copied into the lookup). */
static bool check_lookup_key(Checker *C, Expr **kp, Type *lt)
{
	Type *kt = lt->key;
	Type *t = check_expr(C, kp, CTX_READ, kt);
	if (is_err(t))
		return false;
	bool fits = kt->kind == TY_STR ? t->kind == TY_STR : kt->kind == TY_BOOL ? t->kind == TY_BOOL : t->kind == TY_INT;
	if (!fits) {
		sema_error((*kp)->loc, "the keys of a '%s' are '%s', not '%s'", ts(lt), ts(kt), ts(t));
		return false;
	}
	if (kt->kind == TY_STR) {
		if (is_place_expr(*kp))
			borrow_place(C, *kp, false, (*kp)->loc, NULL);
		return true;
	}
	return coerce(C, kp, kt, "a key");
}

static Type *lookup_member(Checker *C, Expr *e, Expr *recv, Type *lt, const char *name)
{
	if (strcmp(name, " next") == 0 && e->kind == EXPR_CALL && e->as.call.args.len == 1) {
		/* a for-in loop's own: the next position in use, from a position */
		Expr *from = e->as.call.args.items[0];
		make_builtin(C, e, BUILTIN_LOOKUP_NEXT, name, recv, from);
		check_expr(C, &e->as.call.args.items[1], CTX_VALUE, NULL);
		convert_to(C, &e->as.call.args.items[1], &t_long);
		return &t_long;
	}
	static const struct { const char *name; Builtin b; int nargs; bool changes; const char *example; } members[] = {
		{ "length", BUILTIN_LOOKUP_LEN, 0, false, "Ages.length" },
		{ "contains", BUILTIN_LOOKUP_CONTAINS, 1, false, "Ages.contains \"Ada\"" },
		{ "remove", BUILTIN_LOOKUP_REMOVE, 1, true, "Ages.remove \"Ada\"" },
		{ "clear", BUILTIN_LOOKUP_CLEAR, 0, true, "Ages.clear" },
		{ "copy", BUILTIN_LOOKUP_COPY, 0, false, "Ages.copy" },
		{ "keys", BUILTIN_LOOKUP_KEYS, 0, false, "Ages.keys" },
		{ "values", BUILTIN_LOOKUP_VALUES, 0, false, "Ages.values" },
	};
	for (size_t m = 0; m < sizeof members / sizeof *members; m++) {
		if (strcmp(name, members[m].name) != 0)
			continue;
		ExprVec given = { 0 };
		if (e->kind == EXPR_CALL)
			given = e->as.call.args;
		if (given.len != members[m].nargs) {
			sema_error(e->loc, "'.%s' takes %d argument%s, but %d %s given (e.g. '%s')", name, members[m].nargs,
				   members[m].nargs == 1 ? "" : "s", given.len, given.len == 1 ? "was" : "were",
				   members[m].example);
			for (int k = 0; k < given.len; k++)
				check_expr(C, &given.items[k], CTX_READ, NULL);
			return &t_error;
		}
		Builtin b = members[m].b;
		bool ok = true;
		if ((b == BUILTIN_LOOKUP_COPY || b == BUILTIN_LOOKUP_VALUES) && !list_elem_copyable(lt->inner)) {
			sema_error(e->loc, "'.%s' copies the values, and the values of a '%s' can't be copied (objects are "
				   "never copied, only moved)", name, ts(lt));
			ok = false;
		}
		if (members[m].changes && !list_change(C, recv, name))
			ok = false;
		make_builtin(C, e, b, name, recv, NULL);
		for (int k = 0; k < given.len; k++) {
			vec_push(C->A, e->as.call.args, given.items[k]);
			if (!check_lookup_key(C, &e->as.call.args.items[k + 1], lt))
				ok = false;
		}
		if (!ok)
			return &t_error;
		switch (b) {
		case BUILTIN_LOOKUP_LEN: return &t_long;
		case BUILTIN_LOOKUP_CONTAINS: return &t_bool;
		case BUILTIN_LOOKUP_COPY: return lt;
		case BUILTIN_LOOKUP_KEYS: return type_list(C->A, lt->key);
		case BUILTIN_LOOKUP_VALUES: return type_list(C->A, lt->inner);
		default: return &t_void;
		}
	}
	sema_error(e->loc, "a lookup has no '.%s' (it has .length, .contains, .remove, .clear, .copy, .keys and "
		   ".values; read and set values with [key])", name);
	return &t_error;
}

/* Can a method of this type call a handler, back into what holds it? */
static bool calls_back(const Type *t)
{
	if (t->kind == TY_HANDLER)
		return true;
	if (t->kind == TY_ARRAY)
		return calls_back(t->inner);
	if (t->kind != TY_OBJECT || t->obj->kind == OBJ_UNION)
		return false;
	ObjectInfo *o = t->obj;
	if (o->visit == 7)
		return false;   /* being looked at already */
	int saved = o->visit;
	o->visit = 7;
	bool yes = false;
	for (int i = 0; i < o->field_count && !yes; i++)
		yes = calls_back(o->fields[i].type);
	o->visit = saved;
	return yes;
}

/* `H.call`, `H.is_set` and `H.clear` of a handler field (or parameter) */
static Type *handler_member(Checker *C, Expr *e, Expr *recv, const char *name)
{
	Builtin b = strcmp(name, "call") == 0 ? BUILTIN_HANDLER_CALL : strcmp(name, "is_set") == 0 ? BUILTIN_HANDLER_IS_SET
		    : strcmp(name, "clear") == 0 ? BUILTIN_HANDLER_CLEAR : BUILTIN_NONE;
	if (!b) {
		sema_error(e->loc, "a handler has no '.%s' (it has .call, .is_set and .clear)", name);
		return &t_error;
	}
	if (e->kind == EXPR_CALL && e->as.call.args.len) {
		sema_error(e->loc, "'.%s' takes no arguments", name);
		return &t_error;
	}
	bool field = recv->kind == EXPR_FIELD;
	if (b == BUILTIN_HANDLER_CLEAR) {
		if (!field || !part_of_self(recv)) {
			sema_error(e->loc, "only this object's own handler fields can be cleared");
			return &t_error;
		}
		PlaceInfo pi = analyze_place(recv);
		if (!pi.mutable) {
			sema_error(e->loc, "can't clear a handler inside a const method");
			return &t_error;
		}
	}
	if (b == BUILTIN_HANDLER_CALL) {
		if (C->fn && C->fn->is_destructor) {
			sema_error(e->loc, "a destructor can't call a handler: the object it would call may be going too");
			return &t_error;
		}
		/* the handler may change the object it belongs to, which holds
		 * this one: as far as loans go, that changes all of it */
		PlaceInfo pi = is_place_expr(recv) ? analyze_place(recv) : (PlaceInfo){ 0 };
		if (field && pi.ok && !pi.mutable) {
			sema_error(e->loc, "can't call a handler from a const method: it can change the object");
			return &t_error;
		}
		if (pi.ok && !C->unevaluated && pi.root->kind != SYM_GLOBAL) {
			record_access(C, pi.root, ACC_BORROW_MUT, recv->loc);
			add_loan(C, pi.root, "", true, recv->loc, NULL);
		}
	}
	make_builtin(C, e, b, name, recv, NULL);
	return b == BUILTIN_HANDLER_IS_SET ? &t_bool : &t_void;
}

/* `.length` and `.copy` of strings and arrays (`e` is the call or field
 * node, rewritten in place; the receiver is already checked). */
/* A kind's or enum's method: the function "<type>.<name>" a library
 * declares, called with the value first. NULL if there isn't one. */
static FnDecl *type_method(Checker *C, const Type *t, const char *name, SrcLoc l)
{
	if (!t->methods)
		return NULL;
	char full[256];
	snprintf(full, sizeof full, "%s.%s", t->methods, name);
	GlobalName *g = find_global(C, full, l);
	return g && g->kind == GN_FN ? g->fn : NULL;
}

/* e (X.name, or X.name args) becomes a call of the method with X first. */
static Type *call_type_method(Checker *C, Expr *e, Expr *recv, FnDecl *fn, const char *name)
{
	ExprVec old = { 0 };
	if (e->kind == EXPR_CALL)
		old = e->as.call.args;
	SrcLoc l = e->loc;
	char *full = arena_alloc(C->A, strlen(recv->ty->methods) + strlen(name) + 2);
	sprintf(full, "%s.%s", recv->ty->methods, name);
	memset(&e->as, 0, sizeof e->as);
	e->kind = EXPR_CALL;
	Expr *callee = ast_expr(C->A, EXPR_IDENT, l);
	callee->as.ident.name = full;
	e->as.call.callee = callee;
	e->as.call.slot = -1;
	e->as.call.fn = fn;
	e->as.call.checked_args = 1;
	vec_push(C->A, e->as.call.args, recv);
	for (int i = 0; i < old.len; i++)
		vec_push(C->A, e->as.call.args, old.items[i]);
	return check_args(C, e, fn, full);
}

static Type *builtin_member(Checker *C, Expr *e, Expr *recv, Type *rt, const char *name, int nargs, Ctx ctx)
{
	FnDecl *tm = type_method(C, rt, name, e->loc);
	if (tm)
		return call_type_method(C, e, recv, tm, name);
	if (rt->kind_name && strcmp(name, "text") != 0) {
		sema_error(e->loc, "a '%s' has no '.%s'", ts(rt), name);
		return &t_error;
	}
	if (rt->kind == TY_LIST)
		return list_member(C, e, recv, rt, name);
	if (rt->kind == TY_LOOKUP)
		return lookup_member(C, e, recv, rt, name);
	if (rt->kind == TY_HANDLER)
		return handler_member(C, e, recv, name);
	if (rt->kind == TY_ADDRESS) {
		if (e->kind == EXPR_CALL && e->as.call.args.len) {
			sema_error(e->loc, "'.%s' takes no arguments", name);
			return &t_error;
		}
		if (strcmp(name, "value") == 0)
			return make_at(C, e, recv, NULL);
		if (strcmp(name, "number") == 0 || strcmp(name, "is_none") == 0) {
			bool num = name[0] == 'n';
			make_builtin(C, e, num ? BUILTIN_ADDRESS_NUMBER : BUILTIN_ADDRESS_IS_NONE, name, recv, NULL);
			return num ? &t_long : &t_bool;
		}
		sema_error(e->loc, "an address has no '.%s' (it has .value, .number and .is_none)", name);
		return &t_error;
	}
	if (rt->kind == TY_FILE) {
		Builtin fb = strcmp(name, "read_line") == 0 ? BUILTIN_FILE_READ_LINE :
			     strcmp(name, "at_end") == 0 ? BUILTIN_FILE_AT_END :
			     strcmp(name, "handle") == 0 && from_header(e->loc) ? BUILTIN_FILE_HANDLE : BUILTIN_NONE;
		if (!fb) {
			sema_error(e->loc, "a file has no '.%s' (it has .read_line, .at_end, .read_bytes, .write_bytes, "
				   ".position, .move_to, .move_to_end and .size; to write lines to it, put it in front of a "
				   "call: 'Log println \"...\"')", name);
			return &t_error;
		}
		if (e->kind == EXPR_CALL && e->as.call.args.len) {
			sema_error(e->loc, "'.%s' takes no arguments", name);
			return &t_error;
		}
		make_builtin(C, e, fb, name, recv, NULL);
		return fb == BUILTIN_FILE_READ_LINE ? &t_str : fb == BUILTIN_FILE_HANDLE ? &t_long : &t_bool;
	}
	bool is_len = strcmp(name, "length") == 0, is_copy = strcmp(name, "copy") == 0;
	static const struct { const char *name; Builtin b; Type *t; } str_members[] = {
		{ "is_int", BUILTIN_STR_IS_INT, &t_bool }, { "is_long", BUILTIN_STR_IS_LONG, &t_bool },
		{ "is_number", BUILTIN_STR_IS_NUMBER, &t_bool }, { "to_int", BUILTIN_STR_TO_INT, &t_int },
		{ "to_long", BUILTIN_STR_TO_LONG, &t_long }, { "to_double", BUILTIN_STR_TO_DOUBLE, &t_double },
	};
	static const struct { const char *name; Builtin b; Type *t; int nargs; bool text_args; } str_tools[] = {
		{ "find", BUILTIN_STR_FIND, &t_long, 1, true },
		{ "contains", BUILTIN_STR_CONTAINS, &t_bool, 1, true },
		{ "starts_with", BUILTIN_STR_STARTS_WITH, &t_bool, 1, true },
		{ "ends_with", BUILTIN_STR_ENDS_WITH, &t_bool, 1, true },
		{ "trim", BUILTIN_STR_TRIM, &t_str, 0, false },
		{ "upper", BUILTIN_STR_UPPER, &t_str, 0, false },
		{ "lower", BUILTIN_STR_LOWER, &t_str, 0, false },
		{ "replace", BUILTIN_STR_REPLACE, &t_str, 2, true },
		{ "repeat", BUILTIN_STR_REPEAT, &t_str, 1, false },
		{ "pad_left", BUILTIN_STR_PAD_LEFT, &t_str, 1, false },
		{ "pad_right", BUILTIN_STR_PAD_RIGHT, &t_str, 1, false },
		{ "centered", BUILTIN_STR_CENTERED, &t_str, 1, false },
		{ "split", BUILTIN_STR_SPLIT, NULL, 1, true },
	};
	if (rt->kind == TY_STR)
		for (size_t i = 0; i < sizeof str_tools / sizeof *str_tools; i++) {
			if (strcmp(name, str_tools[i].name) != 0)
				continue;
			int want = str_tools[i].nargs;
			ExprVec given = { 0 };
			if (e->kind == EXPR_CALL)
				given = e->as.call.args;
			if (given.len != want) {
				static const char *const examples[] = {
					[BUILTIN_STR_FIND - BUILTIN_STR_FIND] = "s.find \"cat\"",
					[BUILTIN_STR_CONTAINS - BUILTIN_STR_FIND] = "s.contains \"cat\"",
					[BUILTIN_STR_STARTS_WITH - BUILTIN_STR_FIND] = "s.starts_with \"Dr \"",
					[BUILTIN_STR_ENDS_WITH - BUILTIN_STR_FIND] = "s.ends_with \".cby\"",
					[BUILTIN_STR_TRIM - BUILTIN_STR_FIND] = "s.trim",
					[BUILTIN_STR_UPPER - BUILTIN_STR_FIND] = "s.upper",
					[BUILTIN_STR_LOWER - BUILTIN_STR_FIND] = "s.lower",
					[BUILTIN_STR_REPLACE - BUILTIN_STR_FIND] = "s.replace \"cat\", \"dog\"",
					[BUILTIN_STR_REPEAT - BUILTIN_STR_FIND] = "s.repeat 3",
					[BUILTIN_STR_PAD_LEFT - BUILTIN_STR_FIND] = "s.pad_left 8",
					[BUILTIN_STR_PAD_RIGHT - BUILTIN_STR_FIND] = "s.pad_right 20",
					[BUILTIN_STR_CENTERED - BUILTIN_STR_FIND] = "s.centered 30",
				};
				const char *ex = str_tools[i].b == BUILTIN_STR_SPLIT ? "s.split \",\""
						 : examples[str_tools[i].b - BUILTIN_STR_FIND];
				sema_error(e->loc, "'.%s' takes %d argument%s, but %d %s given (e.g. '%s')", name, want,
					   want == 1 ? "" : "s", given.len, given.len == 1 ? "was" : "were", ex);
				for (int k = 0; k < given.len; k++)
					check_expr(C, &given.items[k], CTX_READ, NULL);
				return &t_error;
			}
			make_builtin(C, e, str_tools[i].b, str_tools[i].name, recv, NULL);
			bool ok = true;
			for (int k = 0; k < given.len; k++) {
				vec_push(C->A, e->as.call.args, given.items[k]);
				Expr **ap = &e->as.call.args.items[k + 1];
				if (str_tools[i].text_args) {
					Type *t = check_expr(C, ap, CTX_READ, NULL);
					if (!is_err(t) && t->kind != TY_STR && !(t->kind == TY_INT && t->is_char)) {
						sema_error((*ap)->loc, "'.%s' looks for text (a 'str' or a 'char'), not '%s'%s",
							   name, ts(t), type_is_arith(t) ? "; use .text for a number" : "");
						ok = false;
					}
					if (is_err(t))
						ok = false;
				} else {
					check_expr(C, ap, CTX_VALUE, &t_long);
					if (!coerce(C, ap, &t_long, str_tools[i].b == BUILTIN_STR_REPEAT ? "the number of times" : "the width"))
						ok = false;
				}
			}
			if (!ok)
				return &t_error;
			return str_tools[i].b == BUILTIN_STR_SPLIT ? type_list(C->A, &t_str) : str_tools[i].t;
		}
	if (rt->kind == TY_STR)
		for (size_t i = 0; i < sizeof str_members / sizeof *str_members; i++)
			if (strcmp(name, str_members[i].name) == 0) {
				if (nargs) {
					sema_error(e->loc, "'.%s' takes no arguments", name);
					return &t_error;
				}
				make_builtin(C, e, str_members[i].b, str_members[i].name, recv, NULL);
				return str_members[i].t;
			}
	if (rt->kind == TY_STR && (is_len || is_copy)) {
		if (nargs) {
			sema_error(e->loc, "'.%s' takes no arguments", name);
			return &t_error;
		}
		make_builtin(C, e, is_len ? BUILTIN_STR_LEN : BUILTIN_STR_COPY, name, recv, NULL);
		return is_len ? &t_long : &t_str;
	}
	if ((rt->kind == TY_ARRAY || rt->kind == TY_SLICE) && is_len) {
		if (nargs) {
			sema_error(e->loc, "'.length' takes no arguments");
			return &t_error;
		}
		if (rt->kind == TY_ARRAY) {
			memset(&e->as, 0, sizeof e->as);
			e->kind = EXPR_INT;
			e->as.int_lit.value = rt->len;
			e->as.int_lit.longs = 1;
			e->as.int_lit.from_length = true;
			return &t_long;
		}
		make_builtin(C, e, BUILTIN_SLICE_LEN, name, recv, NULL);
		return &t_long;
	}
	if ((rt->kind == TY_INT || rt->kind == TY_FLOAT || rt->kind == TY_BOOL) && strcmp(name, "text") == 0) {
		if (nargs == 1 && rt->kind != TY_BOOL && !rt->is_char) {
			/* Price.text 2: with that many decimal places */
			Expr **pp = &e->as.call.args.items[0];
			Type *pt = check_expr(C, pp, CTX_VALUE, NULL);
			if (is_err(pt))
				return &t_error;
			if (pt->kind != TY_INT || pt->is_char) {
				sema_error((*pp)->loc, "'.text' takes how many decimal places, a whole number, not '%s'", ts(pt));
				return &t_error;
			}
			convert_to(C, pp, &t_long);
			Expr *places = *pp;
			Type *nt = rt->kind == TY_FLOAT ? &t_double : type_promote(rt);
			convert_to(C, &recv, nt);
			make_builtin(C, e, BUILTIN_TEXT_DECIMALS, name, recv, places);
			return &t_str;
		}
		if (nargs) {
			sema_error(e->loc, "'.text' takes no arguments, or how many decimal places: 'Price.text 2'");
			return &t_error;
		}
		make_builtin(C, e, BUILTIN_TEXT, name, recv, NULL);
		return &t_str;
	}
	if (rt->kind == TY_STR)
		sema_error(e->loc, "a 'str' has no '.%s' (manual chapter 12 lists what it has: .length, .find, "
			   ".contains, .replace, .to_int and more)", name);
	else if (type_is_arith(rt))
		sema_error(e->loc, "a number has '.text' (it as a string), but no '.%s'", name);
	else if (rt->kind == TY_ARRAY || rt->kind == TY_SLICE)
		sema_error(e->loc, "an array has '.length', but no '.%s'; index it with [i]", name);
	else
		return NULL;
	return &t_error;
}

/* Does `name { }` run when an `o` is made (the object's own short name)? */
static bool makes(ObjectInfo *o, const char *name)
{
	for (int k = 0; k < o->mro_len; k++) {
		FnDecl *mk = o->mro[k]->decl->make_fn;
		if (mk && strcmp(mk->name, name) == 0)
			return true;
	}
	return false;
}

/* Is the code being checked inside `owner`: one of its methods, or a
 * method of an object made from it? */
static bool inside(Checker *C, ObjectInfo *owner)
{
	ObjectInfo *here = C->self_obj;
	if (!here && C->fn && C->fn->owner && C->fn->owner->ty)
		here = C->fn->owner->ty->obj;   /* a static method */
	return here && derives_from(here, owner->name);
}

/* A worker's own field or method (not threads.worker's), used other than
 * through `self`: checked as the program runs, as it's only the worker's
 * while it works (use threads) */
static bool worker_checked(ObjectInfo *o, Expr *recv, ObjectInfo *owner)
{
	return o && o->kind == OBJ_OBJECT && recv->kind != EXPR_SELF && derives_from(o, "threads.worker") &&
	       strcmp(owner->name, "threads.worker") != 0;
}

/* threads.worker's done handler (and the method that calls it) are for
 * threads.worker's own code: a worker's `work` calling it would run a
 * window's method on the worker's thread */
static void worker_done_only(Checker *C, SrcLoc l, ObjectInfo *owner, const char *name)
{
	if (strcmp(owner->name, "threads.worker") != 0 ||
	    (strcmp(name, "DoneHandler") != 0 && strcmp(name, "say_done") != 0 &&
	     strcmp(name, "MessageHandler") != 0 && strcmp(name, "say_message") != 0))
		return;
	ObjectInfo *here = C->self_obj;
	if (!here && C->fn && C->fn->owner && C->fn->owner->ty)
		here = C->fn->owner->ty->obj;
	if (!here || strcmp(here->name, "threads.worker") != 0)
		sema_error(l, "'%s' is threads.worker's own: the handlers given to 'on_done' and 'on_message' are "
			   "called by it, on the main program's thread", name);
}

static void inner_error(Checker *C, SrcLoc l, const char *what, const char *name, ObjectInfo *owner)
{
	(void)C;
	sema_error(l, "'%s' is an inner %s of '%s': only %s's own methods (and those of objects made from it) "
		   "can use it", name, what, owner->name, owner->name);
}

static Type *check_method_call_on(Checker *C, Expr *call, bool recv_checked);

static Type *check_method_call(Checker *C, Expr *call) { return check_method_call_on(C, call, false); }

/* json.from P and J.fill P: the functions Cabby wrote for P's struct
 * (parser.c, with `use json`): json.from_struct_<name> and
 * json.fill_struct_<name>. `recv` is J for fill (checked already). */
static Type *struct_json_call(Checker *C, Expr *call, Expr *recv, bool fill)
{
	ExprVec *args = &call->as.call.args;
	const char *what = fill ? "'.fill'" : "'json.from'";
	if (args->len != 1) {
		sema_error(call->loc, "%s takes one struct, e.g. %s", what, fill ? "'Saved.fill Player'" : "'json.from Player'");
		for (int i = 0; i < args->len; i++)
			check_expr(C, &args->items[i], CTX_READ, NULL);
		args->len = 0;   /* (checked: not again) */
		return &t_error;
	}
	Expr **sp = &args->items[0];
	bool place = (*sp)->kind == EXPR_IDENT || (*sp)->kind == EXPR_SELF || (*sp)->kind == EXPR_FIELD ||
		     (*sp)->kind == EXPR_INDEX;
	Type *t = check_expr(C, sp, place ? CTX_PLACE : CTX_READ, NULL);
	args->len = 0;   /* (checked: not again, if this is an error) */
	if (is_err(t))
		return &t_error;
	Type *st = t;
	while (st->kind == TY_REF)
		st = st->inner;
	if (st->kind != TY_OBJECT || st->obj->kind != OBJ_STRUCT) {
		if (fill)
			sema_error((*sp)->loc, "'.fill' fills in a struct's fields from the JSON, so it takes a struct, not "
				   "a '%s' (for one value, use .str, .long, .double or .bool)", ts(st));
		else
			sema_error((*sp)->loc, "'json.from' makes JSON from a struct's fields, so it takes a struct, not a "
				   "'%s' (text, numbers and true/false become json by themselves: json J = 5)", ts(st));
		return &t_error;
	}
	const char *sname = st->obj->name;
	char full[300];
	snprintf(full, sizeof full, "json.%s_struct_%s", fill ? "fill" : "from", sname);
	for (char *p = full + 5; *p; p++)
		if (*p == '.')
			*p = '_';
	GlobalName *g = find_global(C, full, call->loc);
	if (!g || g->kind != GN_FN) {
		const char *why = "it isn't plain data";
		for (int i = 0; i + 1 < C->prog->json_skipped.len; i += 2)
			if (strcmp(C->prog->json_skipped.items[i], sname) == 0)
				why = C->prog->json_skipped.items[i + 1];
		sema_error((*sp)->loc, "a '%s' can't go to and from JSON by itself: %s", sname, why);
		sema_note((*sp)->loc, "JSON holds text, numbers, true/false, enums (by name), other structs, and lists, "
			  "lookups with str keys and arrays of those; build the JSON yourself for anything else");
		return &t_error;
	}
	Expr *callee = ast_expr(C->A, EXPR_IDENT, call->loc);
	callee->as.ident.name = arena_strndup(C->A, full, strlen(full));
	Expr *s_arg = *sp;
	call->as.call.callee = callee;
	call->as.call.args = (ExprVec){ 0 };
	if (fill)
		vec_push(C->A, call->as.call.args, recv);
	vec_push(C->A, call->as.call.args, s_arg);
	call->as.call.checked_args = fill ? 3 : 1;
	call->as.call.fn = g->fn;
	call->as.call.slot = -1;
	return check_args(C, call, g->fn, fill ? "fill" : "json.from");
}



static Type *check_method_call_on(Checker *C, Expr *call, bool recv_checked)
{
	Expr *callee = call->as.call.callee;
	Expr **rp = &callee->as.field.object;
	Expr *recv = *rp;
	const char *name = callee->as.field.name;

	/* T.method(...) — a static method called through its type */
	if (!recv_checked && recv->kind == EXPR_IDENT && !lookup_local(C, recv->as.ident.name) &&
	    !(C->self_obj && lookup_field(C->self_obj, recv->as.ident.name))) {
		TypeName *tn = find_type(C, recv->as.ident.name);
		if (tn && tn->obj) {
			ObjectInfo *o = tn->obj;
			FnDecl *m = o->kind == OBJ_OBJECT ? lookup_method(o, name) : NULL;
			char fname[160];
			snprintf(fname, sizeof fname, "%s.%s", o->name, name);
			if (!m) {
				sema_error(callee->loc, "'%s' has no method '%s'", o->name, name);
				return &t_error;
			}
			if (m->self_kind != SELF_NONE) {
				sema_error(callee->loc, "'%s' is not static; call it on a value, e.g. 'x.%s(...)'", fname, name);
				return &t_error;
			}
			if (m->is_inner && !inside(C, m->owner->ty->obj))
				inner_error(C, callee->loc, "method", name, m->owner->ty->obj);
			call->as.call.fn = m;
			call->as.call.is_static = true;
			return check_args(C, call, m, fname);
		}
	}

	/* (a slice, s[a to b] or Words[2 to 4], is a new value, not a place:
	 * Words[2 to 4].join " " reads it) */
	bool place = recv->kind == EXPR_IDENT || recv->kind == EXPR_SELF || recv->kind == EXPR_FIELD ||
		     (recv->kind == EXPR_INDEX && !recv->as.index.is_slice);
	if (recv->kind == EXPR_INDEX && recv->as.index.is_slice) {
		static const char *const changing[] = { "add", "insert", "remove_at", "take_last", "clear", "sort", "reverse" };
		for (size_t k = 0; k < sizeof changing / sizeof *changing; k++)
			if (strcmp(name, changing[k]) == 0) {
				sema_error(recv->loc, "'.%s' would change a slice, a new list that's then thrown away; keep "
					   "it first (list str Part = Words[1 to 3]), or change the list itself", name);
				return &t_error;
			}
	}
	Type *rt = recv_checked ? recv->ty : check_expr(C, rp, place ? CTX_PLACE : CTX_READ, NULL);
	recv = *rp;
	if (!is_err(rt) && (rt->kind == TY_STR || rt->kind == TY_ARRAY || rt->kind == TY_SLICE || rt->kind == TY_LIST ||
			    rt->kind == TY_LOOKUP || rt->kind == TY_FILE || rt->kind == TY_HANDLER || rt->kind == TY_ADDRESS ||
			    type_is_arith(rt))) {
		Type *bt = builtin_member(C, call, recv, rt, name, call->as.call.args.len, CTX_READ);
		if (bt)
			return bt;
	}
	if (is_err(rt)) {
		for (int i = 0; i < call->as.call.args.len; i++)
			check_expr(C, &call->as.call.args.items[i], CTX_READ, NULL);
		return &t_error;
	}
	if (rt->kind != TY_OBJECT || rt->obj->kind != OBJ_OBJECT) {
		if (rt->kind == TY_OBJECT && lookup_field(rt->obj, name))
			sema_error(callee->loc, "'%s' is a field, not a method", name);
		else if (rt->kind == TY_OBJECT)
			sema_error(callee->loc, "a %s has no methods; use an object for that", ast_object_kind_name(rt->obj->kind));
		else
			sema_error(callee->loc, "type '%s' has no methods", ts(rt));
		return &t_error;
	}
	ObjectInfo *o = rt->obj;
	FnDecl *m = lookup_method(o, name);
	if (!m && strcmp(o->name, "json") == 0 && strcmp(name, "fill") == 0)
		return struct_json_call(C, call, recv, true);
	if (!m) {
		if (lookup_field(o, name))
			sema_error(callee->loc, "'%s' is a field of '%s', not a method", name, o->name);
		else if (o->decl->drop_fn && name[0] == '~')
			sema_error(callee->loc, "the destructor can't be called directly; it runs automatically");
		else if (makes(o, name))
			sema_error(callee->loc, "'%s { }' runs by itself when the object is made; it can't be called", name);
		else
			sema_error(callee->loc, "'%s' has no method '%s'", o->name, name);
		return &t_error;
	}
	if (m->self_kind == SELF_NONE) {
		sema_error(callee->loc, "'%s' is a static method; call it as '%s.%s(...)'", name, m->owner->name, name);
		return &t_error;
	}
	if (m->is_inner && !inside(C, m->owner->ty->obj))
		inner_error(C, callee->loc, "method", name, m->owner->ty->obj);
	call->as.call.worker_check = worker_checked(o, recv, m->owner->ty->obj);
	worker_done_only(C, callee->loc, m->owner->ty->obj, name);
	call->as.call.fn = m;
	call->as.call.slot = m->vtable_slot;
	char fname[160];
	snprintf(fname, sizeof fname, "%s.%s", o->name, name);
	Type *ret = check_args(C, call, m, fname);

	/* the receiver is lent after the arguments are evaluated (two-phase) */
	bool want_mut = m->self_kind == SELF_MUT_REF;
	PlaceInfo pi = is_place_expr(recv) ? analyze_place(recv) : (PlaceInfo){ 0 };
	bool through_ref = pi.ok && (recv->kind == EXPR_IDENT || recv->kind == EXPR_SELF) &&
			   pi.root->type->kind == TY_REF;
	if (through_ref) {
		call->as.call.recv = RECV_PASS_REF;
		if (want_mut && !pi.mutable) {
			if (pi.root->kind == SYM_SELF)
				sema_error(recv->loc, "can't call '%s' from a const method: it can change the object", name);
			else
				sema_error(recv->loc, "can't call '%s' through a read-only borrow: it can change the object", name);
			explain_immutable(&pi);
		} else if (!C->unevaluated && pi.root->kind != SYM_GLOBAL) {
			/* a method that can change the object changes all of it, as far
			 * as any loan on part of it is concerned */
			record_access(C, pi.root, want_mut ? ACC_BORROW_MUT : ACC_READ, recv->loc);
			if (want_mut)
				add_loan(C, pi.root, "", true, recv->loc, NULL);
		}
	} else {
		call->as.call.recv = want_mut ? RECV_AUTOREF_MUT : RECV_AUTOREF;
		if (pi.ok) {
			if (want_mut && !pi.mutable) {
				sema_error(recv->loc, "can't call '%s' on '%s': the method can change the object, but '%s' is %s",
					   name, pi.root->name, pi.root->name, pi.via_ref ? "a read-only borrow" : "const");
				explain_immutable(&pi);
			}
			if (want_mut && pi.mutable && calls_back(rt) && !C->unevaluated && pi.root->kind != SYM_GLOBAL) {
				/* it may call back the object it's part of: that changes
				 * all of the variable, as far as loans go */
				record_access(C, pi.root, ACC_BORROW_MUT, recv->loc);
				add_loan(C, pi.root, "", true, recv->loc, NULL);
			} else {
				borrow_place(C, recv, want_mut && pi.mutable, recv->loc, NULL);
			}
		}
	}
	/* (a value of an object others are made from may be one of them: its
	 * own method, then, as through a reference) */
	call->as.call.is_virtual = through_ref || (pi.ok && pi.via_ref) || o->has_derived;
	return ret;
}

/* `f -x` was parsed as a call because some function or method called `f`
 * takes arguments. If this `f` doesn't (a variable, or a function with no
 * parameters), it was a subtraction after all. */
static bool minus_call_is_subtraction(Checker *C, const Expr *e)
{
	const Expr *callee = e->as.call.callee;
	if (callee->kind != EXPR_IDENT)
		return false;
	const char *name = callee->as.ident.name;
	if (lookup_local(C, name))
		return true;
	if (C->self_obj) {
		FnDecl *m = lookup_method(C->self_obj, name);
		if (m)
			return m->params.len == 0;
		if (lookup_field(C->self_obj, name))
			return true;
	}
	if (find_builtin(name))
		return false;
	SrcLoc quiet = { -1, 0, 0 };
	GlobalName *g = find_global(C, name, quiet);
	if (!g)
		return false;
	if (g->kind == GN_FN)
		return g->fn->params.len == 0 && !g->fn->is_variadic;
	return true;
}

/* Rebuilds `f -a * b + c` (parsed as the call f(-a * b + c)) as the
 * subtraction (f - a * b) + c: the minus becomes binary, taking with it
 * only the multiplications that bind tighter than it. */
static Expr *minus_call_as_subtraction(Checker *C, Expr *e)
{
	Expr *arg = e->as.call.args.items[0];
	Expr **spine[256];
	int n = 0;
	Expr **slot = &arg;
	while ((*slot)->kind == EXPR_BINARY && !(*slot)->parenthesized && n < 255) {
		spine[n++] = slot;
		slot = &(*slot)->as.binary.lhs;
	}
	Expr *neg = *slot;
	if (neg->kind != EXPR_UNARY || neg->as.unary.op != UN_NEG)
		return NULL;
	*slot = neg->as.unary.operand;
	Expr **top = slot;
	for (int i = n - 1; i >= 0; i--) {
		BinaryOp op = (*spine[i])->as.binary.op;
		if (op != BIN_MUL && op != BIN_DIV && op != BIN_MOD)
			break;
		top = spine[i];
	}
	Expr *sub = ast_expr(C->A, EXPR_BINARY, neg->loc);
	sub->as.binary.op = BIN_SUB;
	sub->as.binary.lhs = e->as.call.callee;
	sub->as.binary.rhs = *top;
	*top = sub;
	return arg;
}

/* `println N -1, 2` where `N` is a variable that shares its name with a
 * function: the parser gave `-1, 2` to `N`; give the `2` back. */
static void return_minus_call_args(Checker *C, Expr *e)
{
	ExprVec *args = &e->as.call.args;
	for (int i = 0; i < args->len; i++) {
		Expr *a = args->items[i];
		if (a->kind != EXPR_CALL || !a->as.call.minus || a->as.call.args.len < 2 ||
		    !minus_call_is_subtraction(C, a))
			continue;
		ExprVec rebuilt = { 0 };
		for (int j = 0; j <= i; j++)
			vec_push(C->A, rebuilt, args->items[j]);
		for (int j = 1; j < a->as.call.args.len; j++)
			vec_push(C->A, rebuilt, a->as.call.args.items[j]);
		for (int j = i + 1; j < args->len; j++)
			vec_push(C->A, rebuilt, args->items[j]);
		a->as.call.args.len = 1;
		*args = rebuilt;
	}
}

/* `Log println "..."`, with Log a file: the call's printing goes into it */
static Type *check_file_stream(Checker *C, Expr *e, Expr *stream, const char *name)
{
	if (e->as.call.args.len != 1) {
		sema_error(e->loc, "put one call after the file '%s', and everything it prints goes into it: "
			   "'%s println \"...\"' or '%s report Code'", name, name, name);
		for (int i = 0; i < e->as.call.args.len; i++)
			check_expr(C, &e->as.call.args.items[i], CTX_READ, NULL);
		return &t_error;
	}
	Expr *arg = e->as.call.args.items[0];
	make_builtin(C, e, BUILTIN_TO_FILE, name, stream, arg);
	Type *st = check_expr(C, &e->as.call.args.items[0], CTX_READ, NULL);
	if (is_err(st))
		return st;
	Expr **ap = &e->as.call.args.items[1];
	Type *t = check_expr(C, ap, CTX_VALUE, NULL);
	if (is_err(t))
		return t;
	if ((*ap)->kind != EXPR_CALL) {
		sema_error((*ap)->loc, "the file '%s' takes what a call prints, so a call goes after it (a function or "
			   "method), not a value", name);
		return &t_error;
	}
	return t;
}

static Type *check_call(Checker *C, Expr **ep)
{
	Expr *e = *ep;
	int mark = C->temps.len;
	Symbol *saved = C->binding;
	C->binding = NULL;
	Type *t = &t_error;
	Expr *callee = e->as.call.callee;
	bool handled = false;

	if (callee->kind == EXPR_IDENT) {
		const char *name = callee->as.ident.name;
		Symbol *local = lookup_local(C, name);
		GlobalName *g;
		Builtin b;
		handled = true;
		Type *lt = local ? local->type : NULL;
		while (lt && lt->kind == TY_REF)
			lt = lt->inner;
		FieldInfo *sf = !local && C->self_obj ? lookup_field(C->self_obj, name) : NULL;
		if ((lt && lt->kind == TY_FILE) || (sf && sf->type->kind == TY_FILE)) {
			/* Log println "...": everything the call prints goes into the file */
			Expr *stream = callee;
			if (sf) {
				stream = ast_expr(C->A, EXPR_FIELD, callee->loc);
				stream->as.field.object = ast_expr(C->A, EXPR_SELF, callee->loc);
				stream->as.field.name = name;
			}
			t = check_file_stream(C, e, stream, name);
		} else if (local) {
			if (e->as.call.bare && e->as.call.args.len == 0)
				sema_error(e->loc, "this statement does nothing ('%s' is a variable)", name);
			else
				sema_error(callee->loc, "'%s' is a variable, not a function", name);
		} else if (C->self_obj && (lookup_method(C->self_obj, name) || lookup_field(C->self_obj, name))) {
			/* bare method name inside a method: self.name(...) */
			Expr *self = ast_expr(C->A, EXPR_SELF, callee->loc);
			Expr *fe = ast_expr(C->A, EXPR_FIELD, callee->loc);
			fe->as.field.object = self;
			fe->as.field.name = name;
			e->as.call.callee = fe;
			handled = false;
			callee = fe;
			FieldInfo *f = lookup_field(C->self_obj, name);
			if (f && e->as.call.bare && e->as.call.args.len == 0) {
				sema_error(e->loc, "this statement does nothing ('%s' is a field)", name);
				handled = true;
			}
		} else if ((b = find_builtin(name)) &&
			   !((b == BUILTIN_ABS || b == BUILTIN_CLOSE_TO || b == BUILTIN_RANDOM || b == BUILTIN_RANDOM_SEED ||
			      b == BUILTIN_EXIT) &&
			     (g = find_global(C, name, callee->loc)) && g->kind == GN_FN)) {
			/* (abs, random and exit are Cabby's own unless the program, or a
			 * header it uses, such as standard, has its own) */
			t = check_builtin(C, e, b);
		} else if ((g = find_global(C, name, callee->loc)) && g->kind == GN_FN) {
			e->as.call.fn = g->fn;
			t = check_args(C, e, g->fn, name);
		} else if (g) {
			if (e->as.call.bare && e->as.call.args.len == 0)
				sema_error(e->loc, "this statement does nothing ('%s' is %s)", name,
					   g->kind == GN_GLOBAL ? "a variable" : "a constant");
			else
				sema_error(callee->loc, "'%s' is not a function", name);
		} else if (strcmp(name, "json.from") == 0 && find_type(C, "json")) {
			t = struct_json_call(C, e, NULL, false);
		} else if (find_type(C, name)) {
			sema_error(callee->loc, "'%s' is a type; to make one, write '%s{...}'", name, name);
		} else {
			sema_error(callee->loc, "unknown function '%s'%s", name, name_hint(name));
		}
		if (handled && is_err(t) && !e->as.call.builtin && !e->as.call.fn)
			for (int i = 0; i < e->as.call.args.len; i++)
				check_expr(C, &e->as.call.args.items[i], CTX_READ, NULL);
	}
	if (!handled) {
		if (callee->kind == EXPR_FIELD) {
			Expr *recv = callee->as.field.object;
			if (e->as.call.bare && e->as.call.args.len == 0 && recv->kind != EXPR_SELF) {
				/* `x.width` alone on a line: a field is not callable */
				Type *rt = NULL;
				(void)rt;
			}
			t = check_method_call(C, e);
		} else {
			sema_error(callee->loc, "this expression can't be called");
		}
	}
	release_temps(C, mark);
	C->binding = saved;
	return t;
}

/* ====================================================== initializers */

static Type *check_init(Checker *C, Expr *e, Type *expected)
{
	Type *T = expected;
	if (e->as.init.type)
		T = resolve_type(C, e->as.init.type, 0, "initializers");
	if (!T) {
		sema_error(e->loc, "can't tell what type this {...} is for; write the type in front, e.g. point{...}");
		for (int i = 0; i < e->as.init.entries.len; i++)
			check_expr(C, &e->as.init.entries.items[i].value, CTX_READ, NULL);
		return &t_error;
	}
	if (is_err(T))
		return T;
	InitVec *en = &e->as.init.entries;

	if (T->kind == TY_OBJECT) {
		ObjectInfo *o = T->obj;
		int n = o->field_count;
		Expr **slots = arena_alloc(C->A, sizeof(Expr *) * (size_t)(n ? n : 1));
		int next = 0;
		if (o->kind == OBJ_UNION && en->len > 1)
			sema_error(e->loc, "a union holds one field at a time; initialize only one");
		for (int i = 0; i < en->len; i++) {
			InitEntry *it = &en->items[i];
			int slot;
			if (it->field) {
				FieldInfo *f = lookup_field(o, it->field);
				if (!f) {
					sema_error(it->loc, "'%s' has no field '%s'", o->name, it->field);
					check_expr(C, &it->value, CTX_READ, NULL);
					continue;
				}
				slot = f->slot;
			} else {
				slot = next;
			}
			if (slot >= n) {
				sema_error(it->loc, "too many values for '%s' (it has %d field%s)", o->name, n, n == 1 ? "" : "s");
				check_expr(C, &it->value, CTX_READ, NULL);
				continue;
			}
			next = slot + 1;
			if (slots[slot]) {
				sema_error(it->loc, "field '%s' is given a value twice", o->fields[slot].name);
				continue;
			}
			if (o->fields[slot].is_inner && !inside(C, o->fields[slot].owner))
				inner_error(C, it->loc, "field", o->fields[slot].name, o->fields[slot].owner);
			Type *ft = o->fields[slot].type;
			check_expr(C, &it->value, CTX_VALUE, ft);
			char what[160];
			snprintf(what, sizeof what, "field '%s' of '%s'", o->fields[slot].name, o->name);
			coerce(C, &it->value, ft, what);
			slots[slot] = it->value;
		}
		e->as.init.slots = slots;
		e->as.init.nslots = n;
		return T;
	}
	if (T->kind == TY_LOOKUP) {
		/* key: value entries; slots hold key, value, key, value ... */
		Expr **slots = arena_alloc(C->A, sizeof(Expr *) * (size_t)(en->len ? 2 * en->len : 1));
		bool ok = true;
		for (int i = 0; i < en->len; i++) {
			InitEntry *it = &en->items[i];
			if (!it->key) {
				sema_error(it->loc, "a lookup's entries are written key: value, e.g. {\"Ada\": 36, \"Alan\": 41}");
				check_expr(C, &it->value, CTX_READ, NULL);
				ok = false;
				continue;
			}
			if (!check_lookup_key(C, &it->key, T))
				ok = false;
			check_expr(C, &it->value, CTX_VALUE, T->inner);
			char what[64];
			snprintf(what, sizeof what, "the value of entry %d", i);
			if (!coerce(C, &it->value, T->inner, what))
				ok = false;
			slots[2 * i] = it->key;
			slots[2 * i + 1] = it->value;
		}
		if (!ok)
			return &t_error;
		e->as.init.slots = slots;
		e->as.init.nslots = 2 * en->len;
		return T;
	}
	for (int i = 0; i < en->len; i++)
		if (en->items[i].key) {
			sema_error(en->items[i].loc, "'key: value' entries are for lookups, not '%s'", ts(T));
			return &t_error;
		}
	if (T->kind == TY_LIST) {
		/* a list's starting elements, each moved in */
		Expr **slots = arena_alloc(C->A, sizeof(Expr *) * (size_t)(en->len ? en->len : 1));
		for (int i = 0; i < en->len; i++) {
			InitEntry *it = &en->items[i];
			if (it->field) {
				sema_error(it->loc, "'.%s =' only works for objects, structs and unions", it->field);
				continue;
			}
			check_expr(C, &it->value, CTX_VALUE, T->inner);
			char what[64];
			snprintf(what, sizeof what, "element %d", i);
			coerce(C, &it->value, T->inner, what);
			slots[i] = it->value;
		}
		e->as.init.slots = slots;
		e->as.init.nslots = en->len;
		return T;
	}
	if (T->kind == TY_ARRAY) {
		uint64_t len = T->len;
		if (len == UINT64_MAX) {
			if (en->len == 0) {
				sema_error(e->loc, "an array needs at least one element to take its size from");
				return &t_error;
			}
			len = (uint64_t)en->len;
			T = type_array(C->A, T->inner, len);
		}
		if ((uint64_t)en->len > len) {
			sema_error(en->items[len].loc, "too many elements: '%s' holds %llu", ts(T), (unsigned long long)len);
			return T;
		}
		Expr **slots = arena_alloc(C->A, sizeof(Expr *) * (size_t)len);
		for (int i = 0; i < en->len; i++) {
			InitEntry *it = &en->items[i];
			if (it->field) {
				sema_error(it->loc, "'.%s =' only works for objects, structs and unions", it->field);
				continue;
			}
			check_expr(C, &it->value, CTX_VALUE, T->inner);
			char what[64];
			snprintf(what, sizeof what, "element %d", i);
			coerce(C, &it->value, T->inner, what);
			slots[i] = it->value;
		}
		e->as.init.slots = slots;
		e->as.init.nslots = (int)len;
		return T;
	}
	sema_error(e->loc, "{...} makes objects, structs, unions, arrays, lists and lookups, not '%s'", ts(T));
	return &t_error;
}

/* ====================================================== expressions */

static Type *check_expr_inner(Checker *C, Expr **ep, Ctx ctx, Type *expected)
{
	Expr *e = *ep;
	switch (e->kind) {
	case EXPR_INT: {
		Type *t = int_literal_type(e, false);
		if (!t) {
			sema_error(e->loc, "this number is too big for any whole-number type: a 'long' goes from -9223372036854775808 to 9223372036854775807, and an 'unsigned long' up to 18446744073709551615");
			return &t_error;
		}
		return t;
	}
	case EXPR_FLOAT: return e->as.float_lit.is_float32 ? &t_float : &t_double;
	case EXPR_BOOL:  return &t_bool;
	case EXPR_CHAR:  return &t_char;
	case EXPR_STRING:
		if (ctx == CTX_VALUE)
			e->to_owned_str = true;   /* a fresh heap copy of the literal */
		return &t_str;

	case EXPR_IDENT:
		return check_ident(C, ep, ctx, expected);

	case EXPR_SELF: {
		Symbol *s = C->fn ? C->fn->self_sym : NULL;
		if (!s) {
			sema_error(e->loc, "'self' is only available inside methods");
			return &t_error;
		}
		e->as.ident.sym = s;
		if (ctx == CTX_VALUE) {
			sema_error(e->loc, "can't take ownership of 'self'; methods borrow their object");
			return &t_error;
		}
		ident_use(C, e, s, ctx);
		return s->type->inner;
	}

	case EXPR_UNARY: {
		Expr **op = &e->as.unary.operand;
		switch (e->as.unary.op) {
		case UN_NEG: {
			if ((*op)->kind == EXPR_INT) {
				Type *t = int_literal_type(*op, true);
				if (!t) {
					sema_error(e->loc, "this number is too big for any whole-number type: a 'long' goes from -9223372036854775808 to 9223372036854775807, and an 'unsigned long' up to 18446744073709551615");
					return &t_error;
				}
				(*op)->ty = t;
				return t;
			}
			Type *t = check_expr(C, op, CTX_VALUE, NULL);
			if (is_err(t))
				return t;
			if (!type_is_arith(t)) {
				sema_error(e->loc, "'-' needs a number, not '%s'", ts(t));
				return &t_error;
			}
			if (t->kind_name) {
				if (t->steps) {
					sema_error(e->loc, "a '%s' can't be negative ('-' works on the '%s' between two)", ts(t),
						   ts(t->steps));
					return &t_error;
				}
				convert_to(C, op, t->base);
				return t;
			}
			Type *r = t->kind == TY_FLOAT ? t : type_promote(t);
			convert_to(C, op, r);
			return r;
		}
		case UN_NOT:
			if (is_err(check_cond(C, op, "the operand of '!'")))
				return &t_error;
			return &t_bool;
		case UN_BITNOT: {
			Type *t = check_expr(C, op, CTX_VALUE, NULL);
			if (is_err(require_int(e, t, "~")))
				return &t_error;
			Type *r = type_promote(t);
			convert_to(C, op, r);
			return r;
		}
		}
		return &t_error;
	}

	case EXPR_BINARY:
		return check_binary(C, e);

	case EXPR_ASSIGN: {
		Expr **tp = &e->as.assign.target;
		Expr **vp = &e->as.assign.value;
		bool compound = e->as.assign.compound;
		if ((*tp)->kind == EXPR_SELF) {
			sema_error((*tp)->loc, "can't assign to 'self'; change its fields instead");
			check_expr(C, vp, CTX_VALUE, NULL);
			return &t_void;
		}
		if ((*tp)->kind == EXPR_INDEX && !(*tp)->as.index.is_slice) {
			/* J["name"] = value, J[2] = value: an object's set_item / set_item_at */
			Type *bt = sema_peek_type(C, (*tp)->as.index.base);
			if (bt && (object_with(bt, "set_item") || object_with(bt, "set_item_at"))) {
				Expr *base = (*tp)->as.index.base, *idx = (*tp)->as.index.index, *val = *vp;
				if (compound) {
					sema_error(e->loc, "'%s=' can't change what's at [...] of a '%s'; read it, then assign",
						   ast_binop_str(e->as.assign.op), ts(bt));
					return &t_void;
				}
				Type *it = sema_peek_type(C, idx);
				if (!it)
					return check_expr(C, &(*tp)->as.index.index, CTX_READ, NULL), &t_void;
				const char *m = it->kind == TY_STR ? "set_item" : "set_item_at";
				if (!object_with(bt, m) || (it->kind != TY_STR && (it->kind != TY_INT || it->is_char || it->kind_name))) {
					sema_error(idx->loc, "a '%s' is indexed by %s, not '%s'", ts(bt),
						   object_with(bt, "set_item") ? "a name (a str)" : "a number", ts(it));
					return &t_void;
				}
				Type *rt = check_expr(C, &base, is_place_expr(base) ? CTX_PLACE : CTX_READ, NULL);
				if (is_err(rt))
					return &t_void;
				call_object_method(C, e, base, m, idx, val);
				return &t_void;
			}
		}
		Type *tt = check_expr(C, tp, CTX_PLACE, NULL);
		Expr *target = *tp;
		if (is_err(tt)) {
			check_expr(C, vp, CTX_VALUE, NULL);
			return &t_void;
		}
		if (target->kind == EXPR_INT && target->as.int_lit.from_length) {
			sema_error(target->loc, "'.length' gives a value; it can't be assigned to");
			check_expr(C, vp, CTX_VALUE, NULL);
			return &t_void;
		}
		if (target->kind == EXPR_CALL && target->as.call.implicit) {
			Expr *cal = target->as.call.callee;
			const char *nm = cal->kind == EXPR_IDENT ? cal->as.ident.name : cal->as.field.name;
			sema_error(target->loc, "'%s' is a %s: it gives a value and can't be assigned to", nm,
				   target->as.call.implicit == 2 ? "method" : "function");
			check_expr(C, vp, CTX_VALUE, NULL);
			return &t_void;
		}
		if (target->kind == EXPR_CALL && target->as.call.builtin > BUILTIN_PRINTLN) {
			Builtin tb = target->as.call.builtin;
			sema_error(target->loc, "'%s%s' gives a value; it can't be assigned to",
				   tb == BUILTIN_READ_LINE || tb == BUILTIN_INPUT || tb == BUILTIN_END_OF_INPUT ? "" : ".",
				   target->as.call.callee->as.ident.name);
			check_expr(C, vp, CTX_VALUE, NULL);
			return &t_void;
		}
		if (!is_place_expr(target)) {
			sema_error(target->loc, "can't assign to a part of a temporary value");
			check_expr(C, vp, CTX_VALUE, NULL);
			return &t_void;
		}
		if (!compound && target->kind == EXPR_INDEX && target->as.index.base->ty &&
		    target->as.index.base->ty->kind == TY_LOOKUP) {
			/* Lookup[k] = v adds k when it's new: a change to the lookup itself */
			target->as.index.lookup_put = true;
		}
		PlaceInfo pi = analyze_place(target);
		if (!pi.mutable && !e->generated) {
			if (pi.root->kind == SYM_SELF)
				sema_error(target->loc, "can't change the object inside a const method");
			else if (pi.via_ref)
				sema_error(target->loc, "can't assign through '%s': it is a read-only borrow", pi.root->name);
			else if (pi.root->is_loop_var)
				sema_error(target->loc, "can't change '%s': a 'for ... in' counter is read-only", pi.root->name);
			else if (pi.root->kind == SYM_PARAM)
				sema_error(target->loc, "can't assign to '%s' because it is const", pi.root->name);
			else
				sema_error(target->loc, "can't assign to '%s' because it is const", pi.root->name);
			explain_immutable(&pi);
		}
		if (tt->kind == TY_HANDLER)
			return check_handler_store(C, e, compound);
		Type *res = &t_void;
		if (compound && tt->kind == TY_STR) {
			Type *vt = check_expr(C, vp, CTX_READ, NULL);
			if (is_err(vt))
				return &t_void;
			if (e->as.assign.op != BIN_ADD) {
				sema_error(e->loc, "'%s=' needs numbers; a 'str' only has '+='", ast_binop_str(e->as.assign.op));
				return &t_void;
			}
			if (vt->kind != TY_STR && !(vt->kind == TY_INT && vt->is_char)) {
				sema_error(e->loc, "'+=' adds a 'str' or a 'char' to a string, not '%s'", ts(vt));
				return &t_void;
			}
			res = &t_str;
		} else if (compound) {
			Type *vt = check_expr(C, vp, CTX_VALUE, NULL);
			if (is_err(vt))
				return &t_void;
			BinaryOp op = e->as.assign.op;
			if (!type_is_arith(tt) || !type_is_arith(vt)) {
				sema_error(e->loc, "'%s=' needs numbers, found '%s' and '%s'", ast_binop_str(op), ts(tt), ts(vt));
				return &t_void;
			}
			if (tt->kind_name || vt->kind_name) {
				bool ratio;
				if ((op == BIN_ADD || op == BIN_SUB || op == BIN_MOD) && kind_takes_numbers(tt) && !vt->kind_name &&
				    written_number(*vp) && vt->kind != TY_BOOL && can_convert(C, *vp, vt, tt->base)) {
					*vp = make_implicit_cast(C, *vp, tt);   /* Walk += 50 */
					vt = tt;
				}
				Type *r = kind_result(op, tt, vt, &ratio);
				if (!r || !type_eq(r, tt)) {
					sema_error(e->loc, "'%s=' can't be used with '%s' and '%s'", ast_binop_str(op), ts(tt), ts(vt));
					return &t_void;
				}
				convert_to(C, vp, kind_base(vt));
				Type *ot = type_common(kind_base(tt), kind_base(vt));
				convert_to(C, vp, ot);
				res = ot;
				goto assigned;
			}
			if ((ast_binop_is_bitwise(op) || op == BIN_MOD) &&
			    (is_err(require_int(target, tt, ast_binop_str(op))) || is_err(require_int(*vp, vt, ast_binop_str(op)))))
				return &t_void;
			Type *ot = (op == BIN_SHL || op == BIN_SHR) ? type_promote(tt) : type_common(tt, vt);
			convert_to(C, vp, (op == BIN_SHL || op == BIN_SHR) ? type_promote(vt) : ot);
			res = ot;   /* the operation's type; the result converts back to the target's */
		} else {
			check_expr(C, vp, CTX_VALUE, tt);
			coerce(C, vp, tt, "the assignment");
		}
	assigned:
		if (pi.root && pi.root->kind != SYM_GLOBAL && !C->unevaluated) {
			if (compound)
				record_access_at(C, pi.root, place_path(C, target), ACC_READ, target->loc);
			bool whole = target->kind == EXPR_IDENT && !pi.via_ref;
			record_access_at(C, pi.root, place_path(C, target), pi.via_ref ? ACC_READ : ACC_WRITE, target->loc);
			(void)whole;
		}
		return res;
	}

	case EXPR_CALL:
		return_minus_call_args(C, e);
		if (e->as.call.minus && minus_call_is_subtraction(C, e)) {
			const char *name = e->as.call.callee->as.ident.name;
			Expr *sub = e->as.call.args.len == 1 ? minus_call_as_subtraction(C, e) : NULL;
			if (!sub) {
				sema_error(e->loc, "'%s' takes no arguments, so the '-' after it subtracts; "
					   "put parentheses around the subtraction so the ',' is not "
					   "read as part of it: '(%s - 1), ...'", name, name);
				return &t_error;
			}
			*ep = sub;
			return check_expr_inner(C, ep, ctx, expected);
		}
		{
			Type *saved = C->call_expected;
			C->call_expected = expected;
			Type *t = check_call(C, ep);
			C->call_expected = saved;
			return t;
		}

	case EXPR_FIELD: {
		Expr **op = &e->as.field.object;
		Ctx bctx = ctx == CTX_PLACE ? CTX_PLACE : CTX_READ;
		Type *ot;
		int acc_before = C->acc.len;
		/* T.CONST style access is not supported; T.method() is handled by calls */
		ot = check_expr(C, op, bctx, NULL);
		if (is_err(ot))
			return ot;
		/* reading a field reads that part of the variable, not all of it:
		 * `Said.add VaultPort.text` reads VaultPort while Said is lent */
		if (bctx == CTX_READ && C->acc.len == acc_before + 1 && ot->kind == TY_OBJECT && is_place_expr(*op)) {
			Access *a = &C->acc.items[acc_before];
			PlaceInfo bpi = analyze_place(*op);
			const char *bp = place_path(C, *op);
			if (bpi.ok && a->sym == bpi.root && a->kind == ACC_READ && strcmp(a->path, bp) == 0 &&
			    lookup_field(ot->obj, e->as.field.name))
				a->path = place_path(C, e);
		}
		if (ot->kind != TY_OBJECT) {
			Type *bt = builtin_member(C, e, *op, ot, e->as.field.name, 0, ctx);
			if (bt)
				return bt;
			sema_error(e->loc, "'%s' has no fields", ts(ot));
			return &t_error;
		}
		FieldInfo *f = lookup_field(ot->obj, e->as.field.name);
		if (!f) {
			if (ot->obj->kind == OBJ_OBJECT && lookup_method(ot->obj, e->as.field.name)) {
				/* a method named without parentheses calls it: `w.area * 2` */
				Expr *call = ast_expr(C->A, EXPR_CALL, e->loc);
				call->as.call.callee = e;
				call->as.call.slot = -1;
				call->as.call.implicit = 2;
				*ep = call;
				int mark = C->temps.len;
				Symbol *saved = C->binding;
				C->binding = NULL;
				Type *t = check_method_call_on(C, call, true);
				release_temps(C, mark);
				C->binding = saved;
				return t;
			}
			if (ot->obj->kind == OBJ_OBJECT && lookup_method(ot->obj, e->as.field.name))
				sema_error(e->loc, "'%s' is a method; it can't be assigned to", e->as.field.name);
			else
				sema_error(e->loc, "'%s' has no field '%s'", ot->obj->name, e->as.field.name);
			return &t_error;
		}
		if (f->is_inner && !inside(C, f->owner))
			inner_error(C, e->loc, "field", f->name, f->owner);
		e->as.field.worker_check = worker_checked(ot->obj, e->as.field.object, f->owner);
		worker_done_only(C, e->loc, f->owner, f->name);
		e->as.field.field_index = f->own_index;
		e->as.field.owner_id = f->owner->id;
		if (ctx == CTX_VALUE && !type_is_copy(f->type)) {
			sema_error(e->loc, "can't move field '%s' (a '%s') out of its %s", f->name, ts(f->type),
				   ast_object_kind_name(ot->obj->kind));
			sema_note(e->loc, "pass it to a 'const %s &' parameter to borrow it instead", ts(f->type));
			return &t_error;
		}
		return f->type;
	}

	case EXPR_INDEX: {
		if (e->as.index.is_slice) {
			/* s[a to b]: a new string, through a builtin */
			Type *bt = check_expr(C, &e->as.index.base, CTX_READ, NULL);
			Expr *ends[2] = { e->as.index.index, e->as.index.to };
			bool ok = !is_err(bt);
			for (int k = 0; k < 2; k++) {
				if (!ends[k])
					continue;
				Type *it = check_expr(C, &ends[k], CTX_VALUE, NULL);
				if (is_err(it))
					ok = false;
				else if (it->kind != TY_INT) {
					sema_error(ends[k]->loc, "a slice's %s must be an integer, not '%s'",
						   k ? "end" : "start", ts(it));
					ok = false;
				} else
					convert_to(C, &ends[k], &t_long);
			}
			if (!ok)
				return &t_error;
			if (ctx == CTX_PLACE) {
				sema_error(e->loc, "a slice is a new string; it can't be assigned to (change "
					   "characters one at a time with s[i] = c)");
				return &t_error;
			}
			if (bt->kind == TY_ARRAY || bt->kind == TY_SLICE) {
				sema_error(e->loc, "slices of arrays aren't in Cabby yet; strings and lists can be sliced");
				return &t_error;
			}
			if (bt->kind == TY_LIST && !list_elem_copyable(bt->inner)) {
				sema_error(e->loc, "a slice of a '%s' would copy its elements, and they can't be copied "
					   "(objects are only moved)", ts(bt));
				return &t_error;
			}
			if (bt->kind != TY_STR && bt->kind != TY_LIST) {
				sema_error(e->loc, "can't slice a '%s'; only strings can be sliced", ts(bt));
				return &t_error;
			}
			int flags = (ends[0] ? 0 : RT_SLICE_NO_FROM) | (ends[1] ? 0 : RT_SLICE_NO_TO);
			for (int k = 0; k < 2; k++)
				if (!ends[k]) {
					ends[k] = ast_expr(C->A, EXPR_INT, e->loc);
					ends[k]->ty = &t_long;
					ends[k]->as.int_lit.longs = 1;
				}
			Expr *fl = ast_expr(C->A, EXPR_INT, e->loc);
			fl->as.int_lit.value = (uint64_t)flags;
			fl->ty = &t_int;
			Expr *base = e->as.index.base;
			make_builtin(C, e, bt->kind == TY_LIST ? BUILTIN_LIST_SLICE : BUILTIN_STR_SLICE, "[to]", base, ends[0]);
			vec_push(C->A, e->as.call.args, ends[1]);
			vec_push(C->A, e->as.call.args, fl);
			return bt->kind == TY_LIST ? bt : &t_str;
		}
		Ctx bctx = ctx == CTX_PLACE ? CTX_PLACE : CTX_READ;
		Type *bt = check_expr(C, &e->as.index.base, bctx, NULL);
		if (!is_err(bt) && (object_with(bt, "item") || object_with(bt, "item_at"))) {
			/* J["name"] and J[2]: an object that can be indexed (a json value)
			 * calls its item (by name) or item_at (by number) method */
			Type *it = sema_peek_type(C, e->as.index.index);
			if (!it)
				return check_expr(C, &e->as.index.index, CTX_READ, NULL);
			const char *m = it->kind == TY_STR ? "item" : "item_at";
			if (!object_with(bt, m) || (it->kind != TY_STR && (it->kind != TY_INT || it->is_char || it->kind_name))) {
				sema_error(e->as.index.index->loc, "a '%s' is indexed by %s, not '%s'", ts(bt),
					   object_with(bt, "item") && object_with(bt, "item_at") ? "a name (a str) or a number"
					   : object_with(bt, "item") ? "a name (a str)" : "a number", ts(it));
				return &t_error;
			}
			Expr *base = e->as.index.base, *idx = e->as.index.index;
			return call_object_method(C, e, base, m, idx, NULL);
		}
		if (!is_err(bt) && bt->kind == TY_ADDRESS) {
			/* A[i]: what's i values along from A */
			Type *it = check_expr(C, &e->as.index.index, CTX_VALUE, NULL);
			if (is_err(it))
				return it;
			if (it->kind != TY_INT) {
				sema_error(e->as.index.index->loc, "an index must be an integer, not '%s'", ts(it));
				return &t_error;
			}
			convert_to(C, &e->as.index.index, &t_long);
			return make_at(C, e, e->as.index.base, e->as.index.index);
		}
		if (!is_err(bt) && bt->kind == TY_LOOKUP) {
			if (e->as.index.lookup_part) {
				/* the key or value at a position (a for-in loop's own reading) */
				check_expr(C, &e->as.index.index, CTX_VALUE, NULL);
				return e->as.index.lookup_part == 1 ? bt->key : bt->inner;
			}
			if (!check_lookup_key(C, &e->as.index.index, bt))
				return &t_error;
			if (ctx == CTX_VALUE && !type_is_copy(bt->inner)) {
				sema_error(e->loc, "can't move a '%s' out of a lookup with [key]", ts(bt->inner));
				sema_note(e->loc, "borrow it with a 'const %s &' variable or parameter, copy it with "
					  ".copy, or take it out with .remove after copying it", ts(bt->inner));
				return &t_error;
			}
			return bt->inner;
		}
		Type *it = check_expr(C, &e->as.index.index, CTX_VALUE, NULL);
		if (!is_err(it)) {
			if (it->kind != TY_INT)
				sema_error(e->as.index.index->loc, "an array index must be an integer, not '%s'", ts(it));
			else
				convert_to(C, &e->as.index.index, &t_long);
		}
		if (is_err(bt))
			return bt;
		if (bt->kind == TY_STR) {
			/* a character: read through a builtin; written by assignment */
			if (ctx == CTX_PLACE) {
				e->as.index.on_str = true;
				return &t_char;
			}
			make_builtin(C, e, BUILTIN_STR_GET, "[]", e->as.index.base, e->as.index.index);
			return &t_char;
		}
		if (bt->kind != TY_ARRAY && bt->kind != TY_SLICE && bt->kind != TY_LIST) {
			sema_error(e->loc, "can't index into a '%s'", ts(bt));
			return &t_error;
		}
		if (bt->kind == TY_LIST && ctx == CTX_VALUE && !type_is_copy(bt->inner)) {
			sema_error(e->loc, "can't move a '%s' out of a list with [i]", ts(bt->inner));
			sema_note(e->loc, "borrow it with a 'const %s &' variable or parameter, copy it with "
				  ".copy, or take it out with .take_last", ts(bt->inner));
			return &t_error;
		}
		int64_t k;
		if (bt->kind == TY_ARRAY && const_eval_int(C, e->as.index.index, &k) && (k < 0 || (uint64_t)k >= bt->len))
			sema_error(e->as.index.index->loc, "index %lld is outside '%s' (positions 0 to %lld)", (long long)k, ts(bt), (long long)bt->len - 1);
		if (ctx == CTX_VALUE && !type_is_copy(bt->inner)) {
			sema_error(e->loc, "can't move a '%s' out of an array", ts(bt->inner));
			sema_note(e->loc, "pass it to a 'const %s &' parameter to borrow it instead", ts(bt->inner));
			return &t_error;
		}
		return bt->inner;
	}

	case EXPR_CAST: {
		Type *from = check_expr(C, &e->as.cast.value, CTX_VALUE, NULL);
		Type *to = resolve_type(C, e->as.cast.target, 0, "casts");
		if (is_err(from) || is_err(to))
			return &t_error;
		if (type_eq(from, to) || (type_is_arith(from) && type_is_arith(to)))
			return to;
		if (from->kind == TY_ADDRESS && to->kind == TY_ADDRESS)
			return to;   /* the same memory, read as another kind */
		sema_error(e->loc, "can't cast '%s' to '%s'; casts convert between numbers, bool and char", ts(from), ts(to));
		return &t_error;
	}

	case EXPR_INIT:
		return check_init(C, e, expected);

	case EXPR_SIZEOF: {
		Type *t;
		C->unevaluated++;
		if (e->as.size_of.type)
			t = resolve_type(C, e->as.size_of.type, 0, "sizeof");
		else
			t = check_expr(C, &e->as.size_of.expr, CTX_READ, NULL);
		C->unevaluated--;
		if (is_err(t))
			return t;
		if (t->kind == TY_VOID) {
			sema_error(e->loc, "'void' has no size");
			return &t_error;
		}
		uint64_t n = type_size(t);
		e->kind = EXPR_INT;
		memset(&e->as, 0, sizeof e->as);
		e->as.int_lit.value = n;
		e->as.int_lit.is_unsigned = true;
		e->as.int_lit.longs = 1;
		return &t_ulong;
	}

	case EXPR_BORROW:
	case EXPR_HANDLER:
	case EXPR_CALLBACK:
	case EXPR_AT:
		return e->ty;
	}
	return &t_error;
}

/* A str variable given where a library's object is wanted (bytes B =
 * Name) goes through <type>.from_str, which only reads it: so it is read,
 * not moved, and stays usable. */
static bool converted_by_reading(Checker *C, const Expr *e, const Type *expected)
{
	if (e->kind != EXPR_IDENT || !expected || expected->kind != TY_OBJECT || expected->obj->kind != OBJ_OBJECT)
		return false;
	Symbol *s = lookup_local(C, e->as.ident.name);
	if (!s)
		return false;
	const Type *vt = s->type->kind == TY_REF ? s->type->inner : s->type;
	if (vt->kind != TY_STR)
		return false;
	char full[256];
	snprintf(full, sizeof full, "%s.from_str", expected->obj->name);
	GlobalName *g = find_global(C, full, e->loc);
	return g && g->kind == GN_FN;
}

Type *check_expr(Checker *C, Expr **ep, Ctx ctx, Type *expected)
{
	if (ctx == CTX_VALUE && converted_by_reading(C, *ep, expected))
		ctx = CTX_READ;
	Type *t = check_expr_inner(C, ep, ctx, expected);
	(*ep)->ty = t;
	Expr *e = *ep;
	if (e->kind == EXPR_CALL && e->as.call.fn && C->fn && !C->unevaluated) {
		Expr *callee = e->as.call.callee;
		bool on_self = e->as.call.implicit == 2 ||
			       (callee->kind == EXPR_FIELD && callee->as.field.object->kind == EXPR_SELF);
		vec_push(C->A, C->calls, ((CallEdge){ C->fn, e->as.call.fn, on_self }));
	}
	return t;
}

/* ---- workers: no global that can change, anywhere `work` reaches ----
 * A worker's thread and the rest of the program run at once, so a global
 * both could change would be changed by two at once. Constants are fine. */

static GlobalUse *global_use_in(Checker *C, FnDecl *fn)
{
	for (int i = 0; i < C->global_uses.len; i++)
		if (C->global_uses.items[i].fn == fn)
			return &C->global_uses.items[i];
	return NULL;
}

/* the first use of such a global reachable from fn (each function looked
 * at once: seen) */
static GlobalUse *reaches_global(Checker *C, ObjectInfo *k, FnDecl *fn, FnDecl ***seen, int *nseen)
{
	if (!fn)
		return NULL;
	if (fn->definition)
		fn = fn->definition;
	for (int i = 0; i < *nseen; i++)
		if ((*seen)[i] == fn)
			return NULL;
	*seen = realloc(*seen, sizeof **seen * (size_t)(*nseen + 1));
	(*seen)[(*nseen)++] = fn;
	GlobalUse *g = global_use_in(C, fn);
	if (g)
		return g;
	for (int i = 0; i < C->calls.len; i++) {
		CallEdge *c = &C->calls.items[i];
		if (c->from != fn)
			continue;
		FnDecl *to = c->to;
		if (c->on_self && to->vtable_slot >= 0 && k->vtable[to->vtable_slot])
			to = k->vtable[to->vtable_slot];
		if ((g = reaches_global(C, k, to, seen, nseen)))
			return g;
	}
	return NULL;
}

void check_workers(Checker *C)
{
	Sema *S = C->S;
	for (int i = 0; i < S->nobjects; i++) {
		ObjectInfo *k = S->objects[i];
		if (k->kind != OBJ_OBJECT || !derives_from(k, "threads.worker") || !strcmp(k->name, "threads.worker"))
			continue;
		if (k->decl->drop_fn)
			sema_error(k->decl->drop_fn->loc, "a worker can't have a destructor of its own: it would run while "
				   "'work' might still be running (its fields are let go of by themselves, once it's done)");
		for (int f = 0; f < k->field_count; f++)
			if (k->fields[f].type->kind == TY_HANDLER && strcmp(k->fields[f].owner->name, "threads.worker") != 0)
				sema_error(k->fields[f].loc, "a worker can't keep a handler: 'work' could call another "
					   "object's method through it, on the worker's thread");
		FnDecl *work = lookup_method(k, "work");
		if (!work || work->vtable_slot < 0 || !k->vtable[work->vtable_slot])
			continue;
		work = k->vtable[work->vtable_slot];
		FnDecl **seen = NULL;
		int nseen = 0;
		GlobalUse *g = reaches_global(C, k, work, &seen, &nseen);
		free(seen);
		if (g) {
			sema_error(g->at, "'%s' is a global that can change, and '%s' (a worker) can reach it from 'work': "
				   "its thread and the rest of the program could change it at once", g->name, k->name);
			sema_note(work->loc, "give the worker what it needs in fields of its own, or make '%s' a const",
				  g->name);
		}
	}
}
