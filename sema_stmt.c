/* sema_stmt.c — stage 3, statements and functions. */
#include "sema_int.h"

/* ====================================================== scopes and symbols */

void push_scope(Checker *C)
{
	Scope *s = arena_alloc(C->A, sizeof *s);
	s->parent = C->scope;
	C->scope = s;
}

void pop_scope(Checker *C) { C->scope = C->scope->parent; }

static bool scope_encloses(const Scope *outer, const Scope *inner)
{
	for (const Scope *s = inner; s; s = s->parent)
		if (s == outer)
			return true;
	return false;
}

Symbol *new_symbol(Checker *C, const char *name, SymbolKind kind, Type *type, bool is_const, SrcLoc loc)
{
	Symbol *s = arena_alloc(C->A, sizeof *s);
	s->name = name;
	s->kind = kind;
	s->type = type;
	s->is_const = is_const;
	s->loc = loc;
	s->scope = C->scope;
	s->decl_pos = ++C->pos;
	s->max_use = s->decl_pos;
	s->index = C->fn->frame.len;
	vec_push(C->A, C->fn->frame, s);
	return s;
}

void declare(Checker *C, Symbol *s)
{
	for (int i = 0; i < C->scope->syms.len; i++)
		if (strcmp(C->scope->syms.items[i]->name, s->name) == 0) {
			sema_error(s->loc, "'%s' is already declared in this block", s->name);
			sema_note(C->scope->syms.items[i]->loc, "previous declaration is here");
			break;
		}
	s->scope = C->scope;
	vec_push(C->A, C->scope->syms, s);
}

Symbol *lookup_local(Checker *C, const char *name)
{
	for (Scope *sc = C->scope; sc; sc = sc->parent)
		for (int i = sc->syms.len - 1; i >= 0; i--)
			if (strcmp(sc->syms.items[i]->name, name) == 0)
				return sc->syms.items[i];
	return NULL;
}

void note_use(Checker *C, Symbol *s, SrcLoc loc)
{
	if (s->kind == SYM_GLOBAL || !C->fn)
		return;
	if (C->pos >= s->max_use) {
		s->max_use = C->pos;
		while (C->last_use.len <= s->index)
			vec_push(C->A, C->last_use, ((SrcLoc){ -1, 0, 0 }));
		C->last_use.items[s->index] = loc;
	}
	LoopInfo *outer = NULL;
	for (LoopInfo *L = C->loop; L; L = L->parent)
		if (L->start > s->decl_pos)
			outer = L;
	if (outer && (!s->ext_loop || outer->start < s->ext_loop->start))
		s->ext_loop = outer;
}

void record_access_at(Checker *C, Symbol *s, const char *path, AccessKind k, SrcLoc loc)
{
	if (s->kind == SYM_GLOBAL || !C->fn || C->unevaluated)
		return;
	Access a = { s, k, ++C->pos, loc, path ? path : "" };
	vec_push(C->A, C->acc, a);
	note_use(C, s, loc);
}

void record_access(Checker *C, Symbol *s, AccessKind k, SrcLoc loc)
{
	record_access_at(C, s, "", k, loc);
}

bool paths_overlap(const char *a, const char *b)
{
	if (!a || !b || !*a || !*b)
		return true;   /* all of a variable overlaps every part of it */
	size_t na = strlen(a), nb = strlen(b), n = na < nb ? na : nb;
	if (strncmp(a, b, n) != 0)
		return false;
	/* "Things" and "Things.x" overlap; "Thing" and "Things" don't */
	return na == nb || (na < nb ? b[n] == '.' : a[n] == '.');
}

void add_source(Checker *C, Symbol *borrower, Symbol *src, SrcLoc loc)
{
	if (!scope_encloses(src->scope, borrower->scope)) {
		sema_error(loc, "'%s' does not live long enough to be borrowed by '%s'", src->name, borrower->name);
		sema_note(borrower->loc, "'%s' is declared in an outer block, so it outlives '%s'", borrower->name, src->name);
		return;
	}
	vec_push(C->A, borrower->sources, src);
}

void add_loan(Checker *C, Symbol *root, const char *path, bool is_mut, SrcLoc loc, Symbol *borrower)
{
	if (root->kind == SYM_GLOBAL || !C->fn || C->unevaluated)
		return;
	Loan l = { 0 };
	l.root = root;
	l.path = path ? path : "";
	l.is_mut = is_mut;
	l.created = l.start = ++C->pos;
	l.end = -1;
	l.borrower = borrower;
	l.loc = loc;
	if (borrower) {
		if (!scope_encloses(root->scope, borrower->scope)) {
			sema_error(loc, "'%s' does not live long enough to be borrowed by '%s'", root->name, borrower->name);
			sema_note(borrower->loc, "'%s' is declared in an outer block, so it outlives '%s'",
				  borrower->name, root->name);
			return;
		}
		for (LoopInfo *L = C->loop; L; L = L->parent)
			if (L->start > borrower->decl_pos)
				l.loop = L;
	} else {
		vec_push(C->A, C->temps, C->loans.len);
	}
	vec_push(C->A, C->loans, l);
}

void release_temps(Checker *C, int mark)
{
	for (int i = mark; i < C->temps.len; i++)
		C->loans.items[C->temps.items[i]].end = C->pos;
	C->temps.len = mark;
}

/* ====================================================== goto bookkeeping */

typedef struct {
	Stmt *stmt;
	Block **blocks;   /* innermost first */
	int *index;
	int depth;
} GotoInfo;

static VEC(GotoInfo) g_gotos;

static void record_goto(Checker *C, Stmt *s)
{
	int depth = 0;
	for (BlockCtx *b = C->bctx; b; b = b->parent)
		depth++;
	GotoInfo gi = { s, arena_alloc(C->A, sizeof(Block *) * (size_t)(depth ? depth : 1)),
			arena_alloc(C->A, sizeof(int) * (size_t)(depth ? depth : 1)), depth };
	int i = 0;
	for (BlockCtx *b = C->bctx; b; b = b->parent, i++) {
		gi.blocks[i] = b->block;
		gi.index[i] = b->index;
	}
	vec_push(C->A, g_gotos, gi);
}

static void resolve_gotos(Checker *C)
{
	for (int g = 0; g < g_gotos.len; g++) {
		GotoInfo *gi = &g_gotos.items[g];
		Stmt *s = gi->stmt;
		Stmt *label = NULL;
		for (int i = 0; i < C->labels.len; i++)
			if (strcmp(C->labels.items[i]->as.label.name, s->as.goto_.label) == 0)
				label = C->labels.items[i];
		if (!label) {
			sema_error(s->loc, "there is no label '%s' in this function", s->as.goto_.label);
			continue;
		}
		Block *lb = label->as.label.block;
		int li = label->as.label.index;
		int d = -1;
		for (int i = 0; i < gi->depth; i++)
			if (gi->blocks[i] == lb)
				d = i;
		if (d < 0) {
			sema_error(s->loc, "'goto %s' can't jump into a block it is not already in", s->as.goto_.label);
			sema_note(label->loc, "the label is here");
			continue;
		}
		if (li <= gi->index[d]) {
			sema_error(s->loc, "'goto' can only jump forward; use a loop to repeat code");
			sema_note(label->loc, "the label is here");
			continue;
		}
		for (int k = gi->index[d] + 1; k < li; k++)
			if (lb->stmts.items[k]->kind == STMT_DECL) {
				Stmt *decl = lb->stmts.items[k];
				sema_error(s->loc, "'goto %s' would jump past the declaration of '%s'", s->as.goto_.label,
					   decl->as.decl.len ? decl->as.decl.items[0]->name : "?");
				sema_note(decl->loc, "declared here; move it before the goto, or into its own block");
				break;
			}
		s->as.goto_.target = label;
		s->as.goto_.target_block = lb;
		s->as.goto_.target_index = li;
	}
}

/* ====================================================== statements */

static void check_block(Checker *C, Block *b);
static void check_stmt(Checker *C, Stmt *s);

/* `auto X = ...` (written by the program, not made by the checker): Cabby
 * asks for the type to be written, and says which it would have been. */
void no_auto(VarDecl *v, Type *t)
{
	if (!v->is_auto || v->name[0] == ' ')
		return;
	if (t && !type_is_error(t))
		sema_error(v->loc, "Cabby has no 'auto': write the type, so the reader sees it ('%s %s = ...')", ts(t),
			   v->name);
	else
		sema_error(v->loc, "Cabby has no 'auto': write the type, so the reader sees it");
}

static void check_static_local(Checker *C, VarDecl *v)
{
	Type *t = v->is_auto ? NULL : resolve_type(C, v->type, RT_ALLOW_UNSIZED, "static variables");
	if (v->init) {
		Type *it = check_expr(C, &v->init, CTX_VALUE, t);
		if (!t || (t->kind == TY_ARRAY && t->len == UINT64_MAX))
			t = it;
		else if (!type_is_error(it))
			coerce(C, &v->init, t, "the initial value");
		if (!type_is_error(it) && !is_constant_expr(C, v->init))
			sema_error(v->init->loc, "a static variable's initial value must be a constant");
	}
	if (!t)
		t = &t_error;
	no_auto(v, t);
	if (!type_is_error(t) && !type_is_plain(t))
		sema_error(v->loc, "static variables must be plain data (numbers, bool, char, enums, and structs, "
			   "unions or arrays of those), not '%s'", ts(t));
	Symbol *s = arena_alloc(C->A, sizeof *s);
	s->name = v->name;
	s->kind = SYM_GLOBAL;
	s->type = t;
	s->is_const = v->is_const;
	s->is_volatile = v->is_volatile;
	s->is_static_local = true;
	s->loc = v->loc;
	s->decl = v;
	s->file = v->loc.file;
	s->index = C->S->globals.len;
	int64_t cv;
	if (v->is_const && type_is_int(t) && v->init && const_eval_int(C, v->init, &cv)) {
		s->has_const_value = true;
		s->const_value = cv;
	}
	vec_push(C->A, C->S->globals, s);
	v->sym = s;
	declare(C, s);
}

static void check_var(Checker *C, VarDecl *v)
{
	if (v->is_static) {
		check_static_local(C, v);
		return;
	}
	Type *t = v->is_auto ? NULL : resolve_type(C, v->type, RT_ALLOW_REF | RT_ALLOW_UNSIZED, "local variables");
	if (t && t->kind == TY_HANDLER) {
		sema_error(v->loc, "a handler can only be a parameter, or a field of an object (not a local variable)");
		t = &t_error;
	}
	char what[160];
	if (v->name[0] == ' ')   /* a for-in loop's own variable */
		snprintf(what, sizeof what, "%s", strcmp(v->name, " by") == 0 || strcmp(v->name, " step") == 0
			 ? "the step" : strcmp(v->name, " end") == 0 || strcmp(v->name, " stop") == 0
			 ? "the end of the range" : "the start of the range");
	else
		snprintf(what, sizeof what, "'%s'", v->name);

	if (t && t->kind == TY_REF) {
		Symbol *s = new_symbol(C, v->name, SYM_LOCAL, t, false, v->loc);
		s->is_volatile = v->is_volatile;
		if (!v->init) {
			sema_error(v->loc, "a reference must say what it borrows: '%s &%s = something'", ts(t->inner), v->name);
		} else {
			Symbol *saved = C->binding;
			C->binding = s;
			bind_ref(C, &v->init, t, s, what);
			C->binding = saved;
		}
		if (v->is_register)
			sema_error(v->loc, "a reference can't be 'register'");
		v->sym = s;
		declare(C, s);
		return;
	}

	Symbol *s = new_symbol(C, v->name, SYM_LOCAL, t ? t : &t_error, v->is_const, v->loc);
	s->is_register = v->is_register;
	s->is_volatile = v->is_volatile;
	if (v->init) {
		Type *it = check_expr(C, &v->init, CTX_VALUE, t);
		if (!t) {
			if (it->kind == TY_VOID) {
				sema_error(v->init->loc, "this expression produces no value");
				it = &t_error;
			}
			t = it;
		} else if (t->kind == TY_ARRAY && t->len == UINT64_MAX) {
			t = it;
		} else {
			coerce(C, &v->init, t, what);
		}
	}
	if (!type_is_error(t) && t->kind == TY_ARRAY && t->len == UINT64_MAX) {
		sema_error(v->loc, "an array without a size needs an initializer list to take its size from");
		t = &t_error;
	}
	no_auto(v, t);
	s->type = t;
	v->sym = s;
	declare(C, s);
}

static void check_switch(Checker *C, Stmt *s)
{
	Type *vt = check_expr(C, &s->as.switch_.value, CTX_VALUE, NULL);
	if (!type_is_error(vt)) {
		if (vt->kind != TY_INT && vt->kind != TY_BOOL) {
			sema_error(s->as.switch_.value->loc, "a switch needs an integer, char or enum value, not '%s'", ts(vt));
			vt = &t_error;
		} else {
			vt = type_promote(vt);
			Expr **vp = &s->as.switch_.value;
			if (!type_eq((*vp)->ty, vt))
				*vp = make_implicit_cast(C, *vp, vt);
		}
	}
	VEC(int64_t) seen = { 0 };
	VEC(SrcLoc) seen_at = { 0 };
	C->breakables++;
	for (int i = 0; i < s->as.switch_.cases.len; i++) {
		SwitchCase *c = &s->as.switch_.cases.items[i];
		c->consts = arena_alloc(C->A, sizeof(int64_t) * (size_t)(c->values.len ? c->values.len : 1));
		for (int j = 0; j < c->values.len; j++) {
			Expr **ep = &c->values.items[j];
			Type *t = check_expr(C, ep, CTX_VALUE, NULL);
			int64_t v = 0;
			if (type_is_error(t))
				continue;
			if (!type_is_int(t) || !const_eval_int(C, *ep, &v)) {
				sema_error((*ep)->loc, "a case label must be a constant integer, char or enum value");
				continue;
			}
			if (!type_is_error(vt))
				coerce(C, ep, vt, "the case label");
			c->consts[j] = v;
			for (int k = 0; k < seen.len; k++)
				if (seen.items[k] == v) {
					sema_error((*ep)->loc, "duplicate case value %lld", (long long)v);
					sema_note(seen_at.items[k], "first used here");
				}
			vec_push(C->A, seen, v);
			vec_push(C->A, seen_at, (*ep)->loc);
		}
		check_block(C, c->body);
	}
	C->breakables--;
}

static void loop_begin(Checker *C, LoopInfo **out)
{
	LoopInfo *L = arena_alloc(C->A, sizeof *L);
	L->parent = C->loop;
	L->start = ++C->pos;
	C->loop = L;
	C->loops++;
	C->breakables++;
	*out = L;
}

static void loop_end(Checker *C, LoopInfo *L)
{
	L->end = ++C->pos;
	C->loop = L->parent;
	C->loops--;
	C->breakables--;
}

/* ---------------------------------------------------------------- for-in
 *
 * `for i in a to b by k { body }` becomes an ordinary for loop over hidden
 * variables (their names contain a space, so a program cannot name them):
 *
 *     for T i = a, T end = b, T by = k, bool go = i <= end && by > 0;
 *         go;
 *         if (U)(end - i) < (U)by { go = false } else { i += by }
 *     { body }
 *
 * T is the counter's type: as written, or else the range's type (the
 * common type of a and b, which keeps a char range char). U is the unsigned
 * type of T's promotion: the distance left is compared before stepping, so
 * the counter never steps past `end`, and the loop cannot run forever or
 * overflow at the ends of T's range (`for unsigned i in 10 down to 0` stops
 * at 0). `in n` counts 0 .. n-1 (stop when end - i <= by); `down to`
 * compares i - end. The counter is read-only to the body; the update is
 * marked `generated` so it may change it. */

static TypeExpr *preset_type(Checker *C, Type *t, SrcLoc l)
{
	TypeExpr *te = ast_type_expr(C->A, TYPEX_PRESET, l);
	te->resolved = t;
	return te;
}

static Expr *gen_ident(Checker *C, const char *name, SrcLoc l)
{
	Expr *e = ast_expr(C->A, EXPR_IDENT, l);
	e->as.ident.name = name;
	return e;
}

static Expr *gen_int(Checker *C, uint64_t v, SrcLoc l)
{
	Expr *e = ast_expr(C->A, EXPR_INT, l);
	e->as.int_lit.value = v;
	return e;
}

static Expr *gen_cast(Checker *C, Type *t, Expr *x, SrcLoc l)
{
	Expr *e = ast_expr(C->A, EXPR_CAST, l);
	e->as.cast.value = x;
	e->as.cast.target = preset_type(C, t, l);
	return e;
}

static Expr *gen_binary(Checker *C, BinaryOp op, Expr *a, Expr *b, SrcLoc l)
{
	Expr *e = ast_expr(C->A, EXPR_BINARY, l);
	e->as.binary.op = op;
	e->as.binary.lhs = a;
	e->as.binary.rhs = b;
	return e;
}

static Stmt *gen_assign(Checker *C, const char *name, bool compound, BinaryOp op, Expr *value, SrcLoc l)
{
	Expr *e = ast_expr(C->A, EXPR_ASSIGN, l);
	e->as.assign.compound = compound;
	e->as.assign.op = op;
	e->as.assign.target = gen_ident(C, name, l);
	e->as.assign.value = value;
	e->generated = true;
	Stmt *st = ast_stmt(C->A, STMT_EXPR, l);
	st->as.expr = e;
	return st;
}

static VarDecl *gen_var(Checker *C, const char *name, Type *t, Expr *init, SrcLoc l)
{
	VarDecl *v = ast_var(C->A, name, l);
	v->type = t ? preset_type(C, t, l) : NULL;
	v->is_auto = t == NULL;
	v->init = init;
	return v;
}

static Type *unsigned_of(Type *t)
{
	t = type_promote(t);
	return t->bits == 64 ? &t_ulong : &t_uint;
}

/* The type an expression would have, without reporting anything or
 * recording any use of it: checks a copy. NULL if it has errors. */
static Expr *peek_checked(Checker *C, Expr *e);

static Type *peek_type(Checker *C, Expr *e)
{
	Expr *c = peek_checked(C, e);
	return c ? c->ty : NULL;
}

Type *sema_peek_type(Checker *C, Expr *e) { return peek_type(C, e); }

/* A checked copy of e (as peek_type), or NULL if it has errors: shows what
 * e really is, such as a member call written like a field (`Ages.keys`). */
static Expr *peek_checked(Checker *C, Expr *e)
{
	Expr *copy = ast_clone_expr(C->A, e);
	DiagMute m = diag_mute();
	int saved = C->unevaluated;
	C->unevaluated = 1;
	int mark = C->temps.len;
	Type *t = check_expr(C, &copy, CTX_READ, NULL);
	release_temps(C, mark);
	C->unevaluated = saved;
	if (diag_unmute(m) || !t)
		return NULL;
	copy->ty = t;
	return copy;
}

/* Whether a for-in loop's source is a variable, field or element, which
 * the loop borrows, rather than a value made for it (a call, `Ages.keys`,
 * `Words.copy`), which the loop holds. */
static bool loop_source_is_place(Checker *C, Expr *E)
{
	Expr *c = peek_checked(C, E);
	if (!c)
		return false;
	switch (c->kind) {
	case EXPR_IDENT: case EXPR_SELF:
		return c->as.ident.sym != NULL;
	case EXPR_FIELD:
		return true;
	case EXPR_INDEX:
		return !c->as.index.is_slice && !c->as.index.on_str;
	default:
		return false;
	}
}

/* `for X in Things`, over a list, an array or a string, becomes
 *
 *     for long k = 0; k < Things.length; k++ {
 *         const T X = Things[k]       (a copy, for numbers, bool and char)
 *         const T &X = Things[k]      (a read-only borrow, for anything else)
 *         body
 *     }
 *
 * (k is the hidden " k"). The loop reads Things through a hidden read-only
 * borrow, " seq", so changing it inside the loop is refused. When Things
 * isn't a variable or field (a call, say), " seq" holds it instead, for the
 * length of the loop. */
static void lower_for_each(Checker *C, Stmt *s, Type *T)
{
	ForRange *r = s->as.for_.range;
	SrcLoc l = s->loc;
	VarDecl *ctr = r->var;
	Type *elem = T->kind == TY_STR ? &t_char : T->inner;
	bool obj = T->kind == TY_OBJECT;   /* one with length and item_at (a json list): items come out owned */
	if (obj) {
		FnDecl *at = lookup_method(T->obj, "item_at");
		elem = at->ret_type;
	}
	Expr *E = r->end;
	bool place = loop_source_is_place(C, E);
	Stmt *init = ast_stmt(C->A, STMT_DECL, l);
	s->as.for_.init = init;
	if (!place) {
		vec_push(C->A, init->as.decl, gen_var(C, " seq", NULL, E, E->loc));
	} else {
		/* a read-only borrow for the whole loop: the list can't change under it */
		VarDecl *seq = ast_var(C->A, " seq", E->loc);
		TypeExpr *ref = ast_type_expr(C->A, TYPEX_REF, E->loc);
		ref->is_const = true;
		ref->inner = preset_type(C, T->kind == TY_SLICE ? T : T, E->loc);
		seq->type = ref;
		seq->is_const = true;
		seq->init = E;
		vec_push(C->A, init->as.decl, seq);
	}
	E = gen_ident(C, " seq", E->loc);
	vec_push(C->A, init->as.decl, gen_var(C, " k", &t_long, gen_int(C, 0, l), l));
	check_stmt(C, init);

	Expr *len = ast_expr(C->A, EXPR_FIELD, l);
	len->as.field.object = ast_clone_expr(C->A, E);
	len->as.field.name = "length";
	s->as.for_.cond = gen_binary(C, BIN_LT, gen_ident(C, " k", l), len, l);
	s->as.for_.update = gen_assign(C, " k", true, BIN_ADD, gen_int(C, 1, l), l);

	Expr *item = ast_expr(C->A, EXPR_INDEX, ctr->loc);
	item->as.index.base = ast_clone_expr(C->A, E);
	item->as.index.index = gen_ident(C, " k", l);
	VarDecl *x = ast_var(C->A, ctr->name, ctr->loc);
	TypeExpr *xt = ctr->type ? ctr->type : preset_type(C, elem, ctr->loc);
	if (type_is_copy(elem) || obj) {
		x->type = xt;
	} else {
		TypeExpr *ref = ast_type_expr(C->A, TYPEX_REF, ctr->loc);
		ref->is_const = true;
		ref->inner = xt;
		x->type = ref;
	}
	x->is_const = !obj;
	x->init = item;
	Stmt *d = ast_stmt(C->A, STMT_DECL, ctr->loc);
	vec_push(C->A, d->as.decl, x);
	Block *body = s->as.for_.body;
	vec_push(C->A, body->stmts, d);   /* grow by one, then move it to the front */
	for (int i = body->stmts.len - 1; i > 0; i--)
		body->stmts.items[i] = body->stmts.items[i - 1];
	body->stmts.items[0] = d;
}

/* `for Key, Value in Things`, over a lookup, becomes
 *
 *     for long k = Things.next(0); k >= 0; k = Things.next(k + 1) {
 *         const K Key = <the key at k>        (or const K &Key, for a str)
 *         const V Value = <the value at k>    (or const V &Value, if V isn't copied)
 *         body
 *     }
 *
 * where next gives the next position in use (removing a key leaves a gap
 * until the lookup tidies itself), or -1 after the last. With one name it
 * goes through the keys alone. Things is held as in lower_for_each. */
static Expr *lookup_next(Checker *C, Expr *seq, Expr *from, SrcLoc l)
{
	Expr *f = ast_expr(C->A, EXPR_FIELD, l);
	f->as.field.object = ast_clone_expr(C->A, seq);
	f->as.field.name = " next";
	Expr *call = ast_expr(C->A, EXPR_CALL, l);
	call->as.call.callee = f;
	call->as.call.slot = -1;
	vec_push(C->A, call->as.call.args, from);
	return call;
}

static VarDecl *loop_item(Checker *C, VarDecl *name, Type *t, Expr *E, int part)
{
	Expr *item = ast_expr(C->A, EXPR_INDEX, name->loc);
	item->as.index.base = ast_clone_expr(C->A, E);
	item->as.index.index = gen_ident(C, " k", name->loc);
	item->as.index.lookup_part = (uint8_t)part;
	VarDecl *x = ast_var(C->A, name->name, name->loc);
	TypeExpr *xt = name->type ? name->type : preset_type(C, t, name->loc);
	if (type_is_copy(t)) {
		x->type = xt;
	} else {
		TypeExpr *ref = ast_type_expr(C->A, TYPEX_REF, name->loc);
		ref->is_const = true;
		ref->inner = xt;
		x->type = ref;
	}
	x->is_const = true;
	x->init = item;
	return x;
}

static void lower_for_lookup(Checker *C, Stmt *s, Type *T)
{
	ForRange *r = s->as.for_.range;
	SrcLoc l = s->loc;
	Expr *E = r->end;
	bool place = loop_source_is_place(C, E);
	Stmt *init = ast_stmt(C->A, STMT_DECL, l);
	s->as.for_.init = init;
	if (!place) {
		vec_push(C->A, init->as.decl, gen_var(C, " seq", NULL, E, E->loc));
	} else {
		VarDecl *seq = ast_var(C->A, " seq", E->loc);
		TypeExpr *ref = ast_type_expr(C->A, TYPEX_REF, E->loc);
		ref->is_const = true;
		ref->inner = preset_type(C, T, E->loc);
		seq->type = ref;
		seq->is_const = true;
		seq->init = E;
		vec_push(C->A, init->as.decl, seq);
	}
	E = gen_ident(C, " seq", E->loc);
	vec_push(C->A, init->as.decl, gen_var(C, " k", &t_long, lookup_next(C, E, gen_int(C, 0, l), l), l));
	check_stmt(C, init);

	s->as.for_.cond = gen_binary(C, BIN_GE, gen_ident(C, " k", l), gen_int(C, 0, l), l);
	Expr *after = gen_binary(C, BIN_ADD, gen_ident(C, " k", l), gen_int(C, 1, l), l);
	s->as.for_.update = gen_assign(C, " k", false, BIN_ADD, lookup_next(C, E, after, l), l);

	Stmt *d = ast_stmt(C->A, STMT_DECL, r->var->loc);
	vec_push(C->A, d->as.decl, loop_item(C, r->var, T->key, E, 1));
	if (r->var2)
		vec_push(C->A, d->as.decl, loop_item(C, r->var2, T->inner, E, 2));
	Block *body = s->as.for_.body;
	vec_push(C->A, body->stmts, d);   /* grow by one, then move it to the front */
	for (int i = body->stmts.len - 1; i > 0; i--)
		body->stmts.items[i] = body->stmts.items[i - 1];
	body->stmts.items[0] = d;
}

/* Builds and checks the loop's declarations; fills in init, cond and update
 * (cond and update are checked by the caller, like any for loop's). */
static void lower_for_range(Checker *C, Stmt *s)
{
	ForRange *r = s->as.for_.range;
	SrcLoc l = s->loc;
	VarDecl *ctr = r->var;
	if (!r->start && !r->step && !r->down && r->end) {
		Type *pt = peek_type(C, r->end);
		if (pt && pt->kind == TY_LOOKUP) {
			lower_for_lookup(C, s, pt);
			return;
		}
		if (pt && r->var2) {
			sema_error(r->var2->loc, "two names ('for Key, Value in ...') go through a lookup; a '%s' takes "
				   "one ('for X in ...')", ts(pt));
			r->var2 = NULL;
		}
		if (pt && (pt->kind == TY_LIST || pt->kind == TY_ARRAY || pt->kind == TY_SLICE || pt->kind == TY_STR)) {
			lower_for_each(C, s, pt);
			return;
		}
		Type *po = pt;
		while (po && po->kind == TY_REF)
			po = po->inner;
		if (po && po->kind == TY_OBJECT && po->obj->kind == OBJ_OBJECT && lookup_method(po->obj, "item_at") &&
		    lookup_method(po->obj, "length")) {
			lower_for_each(C, s, po);   /* `for Item in J`: a json list, through length and item_at */
			return;
		}
	}
	if (r->var2) {
		sema_error(r->var2->loc, "two names ('for Key, Value in ...') go through a lookup, not a range of numbers");
		r->var2 = NULL;
	}
	Stmt *init = ast_stmt(C->A, STMT_DECL, l);
	s->as.for_.init = init;

	/* 1. without a counter type: the range as written, to learn its types.
	 * With one, the range converts to it by the usual rules instead. */
	bool explicit_type = ctr->type != NULL;
	Stmt *raw = ast_stmt(C->A, STMT_DECL, l);
	VarDecl *vs = NULL, *ve = NULL, *vk = NULL;
	if (!explicit_type) {
		vs = r->start ? gen_var(C, " start", NULL, r->start, r->start->loc) : NULL;
		ve = gen_var(C, " stop", NULL, r->end, r->end->loc);
		vk = r->step ? gen_var(C, " step", NULL, r->step, r->step->loc) : NULL;
		if (vs)
			vec_push(C->A, raw->as.decl, vs);
		vec_push(C->A, raw->as.decl, ve);
		if (vk)
			vec_push(C->A, raw->as.decl, vk);
		check_stmt(C, raw);
	}

	Type *tstart = vs && vs->sym ? vs->sym->type : NULL;
	Type *te = ve && ve->sym ? ve->sym->type : &t_error;
	Type *T;
	if (explicit_type) {
		T = resolve_type(C, ctr->type, 0, "loop counters");
	} else if (type_is_error(te) || (tstart && type_is_error(tstart))) {
		T = &t_error;
	} else if (!tstart || type_eq(tstart, te)) {
		T = tstart ? te : (te->kind == TY_INT ? type_promote(te) : te);
	} else if (type_is_int(tstart) && type_is_int(te)) {
		T = type_common(type_promote(tstart), type_promote(te));
	} else {
		T = !type_is_int(tstart) ? tstart : te;
	}
	if (!type_is_error(T) && T->kind != TY_INT) {
		sema_error(ctr->type ? ctr->type->loc : (vs && !type_is_int(tstart) ? r->start->loc : r->end->loc),
			   "a 'for ... in' loop counts in whole numbers, not '%s'; use a C-style for "
			   "('for double x = 0.0; x < 1.0; x += 0.1') for other steps", ts(T));
		T = &t_error;
	}
	if (vk && vk->sym && !type_is_error(vk->sym->type)) {
		int64_t k;
		if (!type_is_int(vk->sym->type))
			sema_error(r->step->loc, "the step after 'by' must be a whole number");
		else if (const_eval_int(C, vk->init, &k) && k <= 0)
			sema_error(r->step->loc, "the step after 'by' must be positive; count down with 'a down to b'");
	}

	/* 2. the loop's own variables, in T */
	Stmt *typed = ast_stmt(C->A, STMT_DECL, l);
	Expr *start, *end, *by;
	if (explicit_type) {
		start = r->start ? r->start : gen_int(C, 0, l);
		end = r->end;
		by = r->step ? r->step : gen_int(C, 1, l);
	} else {
		start = vs ? gen_cast(C, T, gen_ident(C, " start", l), r->start->loc) : gen_int(C, 0, l);
		end = gen_cast(C, T, gen_ident(C, " stop", l), r->end->loc);
		by = vk ? gen_cast(C, T, gen_ident(C, " step", l), r->step->loc) : gen_int(C, 1, l);
	}
	VarDecl *vi = gen_var(C, ctr->name, T, start, ctr->loc);
	vi->is_const = true;
	VarDecl *vend = gen_var(C, " end", T, end, l);
	VarDecl *vby = gen_var(C, " by", T, by, l);
	BinaryOp first = r->down ? BIN_GE : r->inclusive ? BIN_LE : BIN_LT;
	Expr *go_init = gen_binary(C, BIN_LOG_AND,
				   gen_binary(C, first, gen_ident(C, ctr->name, l), gen_ident(C, " end", l), l),
				   gen_binary(C, BIN_GT, gen_ident(C, " by", l), gen_int(C, 0, l), l), l);
	VarDecl *vgo = gen_var(C, " go", &t_bool, go_init, l);
	vec_push(C->A, typed->as.decl, vi);
	vec_push(C->A, typed->as.decl, vend);
	vec_push(C->A, typed->as.decl, vby);
	vec_push(C->A, typed->as.decl, vgo);
	if (type_is_error(T)) {
		/* still check the body, with the counter declared */
		vi->init = NULL;
		vi->type = preset_type(C, &t_error, ctr->loc);
		typed->as.decl.len = 1;
	}
	check_stmt(C, typed);
	if (vi->sym)
		vi->sym->is_loop_var = true;
	if (explicit_type && r->step && !type_is_error(T)) {
		int64_t k;
		if (const_eval_int(C, vby->init, &k) && k <= 0)
			sema_error(r->step->loc, "the step after 'by' must be positive; count down with 'a down to b'");
	}

	for (int i = 0; i < raw->as.decl.len; i++)
		vec_push(C->A, init->as.decl, raw->as.decl.items[i]);
	for (int i = 0; i < typed->as.decl.len; i++)
		vec_push(C->A, init->as.decl, typed->as.decl.items[i]);
	if (type_is_error(T))
		return;

	/* 3. condition and step */
	s->as.for_.cond = gen_ident(C, " go", l);
	Type *U = unsigned_of(T);
	Expr *left = r->down ? gen_binary(C, BIN_SUB, gen_ident(C, ctr->name, l), gen_ident(C, " end", l), l)
			     : gen_binary(C, BIN_SUB, gen_ident(C, " end", l), gen_ident(C, ctr->name, l), l);
	BinaryOp stop = (!r->down && !r->inclusive) ? BIN_LE : BIN_LT;
	Expr *last = gen_binary(C, stop, gen_cast(C, U, left, l), gen_cast(C, U, gen_ident(C, " by", l), l), l);
	last->parenthesized = true;
	Stmt *upd = ast_stmt(C->A, STMT_IF, l);
	upd->as.if_.cond = last;
	upd->as.if_.then_block = ast_block(C->A, l);
	Expr *f = ast_expr(C->A, EXPR_BOOL, l);
	f->as.bool_val = false;
	vec_push(C->A, upd->as.if_.then_block->stmts, gen_assign(C, " go", false, BIN_ADD, f, l));
	Stmt *els = ast_stmt(C->A, STMT_BLOCK, l);
	els->as.block = ast_block(C->A, l);
	vec_push(C->A, els->as.block->stmts,
		 gen_assign(C, ctr->name, true, r->down ? BIN_SUB : BIN_ADD, gen_ident(C, " by", l), l));
	upd->as.if_.else_branch = els;
	s->as.for_.update = upd;
}

/* `... else`: its value checked as the variable's, or its block */
static void check_recover(Checker *C, Stmt *s)
{
	Recover *r = s->recover;
	if (s->kind == STMT_DECL) {
		VarDecl *v = s->as.decl.items[0];
		Type *t = v->sym ? v->sym->type : &t_error;
		if (t->kind == TY_REF || v->is_static) {
			sema_error(r->loc, "'else' can't follow %s", t->kind == TY_REF ? "a reference's declaration (it borrows, so it can't fail)"
				   : "a static variable's declaration");
			return;
		}
		if (r->value) {
			int mark = C->temps.len;
			char what[160];
			snprintf(what, sizeof what, "'%s' (after 'else')", v->name);
			check_expr(C, &r->value, CTX_VALUE, t);
			if (!type_is_error(t))
				coerce(C, &r->value, t, what);
			release_temps(C, mark);
		}
	}
	if (r->block)
		check_block(C, r->block);
}

static void check_stmt(Checker *C, Stmt *s)
{
	if (s->recover && (s->kind == STMT_DECL || s->kind == STMT_EXPR)) {
		Recover *r = s->recover;
		s->recover = NULL;
		check_stmt(C, s);
		s->recover = r;
		check_recover(C, s);
		return;
	}
	switch (s->kind) {
	case STMT_DECL:
		for (int i = 0; i < s->as.decl.len; i++) {
			int mark = C->temps.len;
			check_var(C, s->as.decl.items[i]);
			release_temps(C, mark);
		}
		break;
	case STMT_EXPR: {
		Expr **ep = &s->as.expr;
		Expr *e = *ep;
		if (e->kind == EXPR_CALL || e->kind == EXPR_ASSIGN) {
			check_expr(C, ep, CTX_VALUE, NULL);
		} else {
			Type *t = check_expr(C, ep, CTX_READ, NULL);
			/* a method or function named alone was called: it does something */
			bool called = (*ep)->kind == EXPR_CALL && (*ep)->as.call.implicit;
			if (!type_is_error(t) && !called)
				sema_warning(e->loc, "this statement does nothing (its value is not used)");
		}
		break;
	}
	case STMT_IF:
		check_cond(C, &s->as.if_.cond, "an 'if' condition");
		check_block(C, s->as.if_.then_block);
		if (s->as.if_.else_branch)
			check_stmt(C, s->as.if_.else_branch);
		break;
	case STMT_BLOCK:
		check_block(C, s->as.block);
		break;
	case STMT_WHILE: {
		LoopInfo *L;
		loop_begin(C, &L);
		int mark = C->temps.len;
		check_cond(C, &s->as.while_.cond, "a 'while' condition");
		release_temps(C, mark);
		check_block(C, s->as.while_.body);
		loop_end(C, L);
		break;
	}
	case STMT_DO: {
		LoopInfo *L;
		loop_begin(C, &L);
		check_block(C, s->as.while_.body);
		int mark = C->temps.len;
		check_cond(C, &s->as.while_.cond, "a 'do ... while' condition");
		release_temps(C, mark);
		loop_end(C, L);
		break;
	}
	case STMT_FOR: {
		push_scope(C);
		Block *scope = ast_block(C->A, s->loc);
		scope->parent = C->bctx ? C->bctx->block : NULL;
		s->as.for_.scope = scope;
		if (s->as.for_.range)
			lower_for_range(C, s);
		else if (s->as.for_.init)
			check_stmt(C, s->as.for_.init);
		if (s->as.for_.init) {
			if (s->as.for_.init->kind == STMT_DECL)
				for (int i = 0; i < s->as.for_.init->as.decl.len; i++) {
					VarDecl *v = s->as.for_.init->as.decl.items[i];
					if (v->sym && v->sym->kind != SYM_GLOBAL)
						vec_push(C->A, scope->locals, v->sym);
				}
		}
		LoopInfo *L;
		loop_begin(C, &L);
		if (s->as.for_.cond) {
			int mark = C->temps.len;
			check_cond(C, &s->as.for_.cond, "a 'for' condition");
			release_temps(C, mark);
		}
		check_block(C, s->as.for_.body);
		if (s->as.for_.update) {
			int mark = C->temps.len;
			check_stmt(C, s->as.for_.update);
			release_temps(C, mark);
		}
		loop_end(C, L);
		pop_scope(C);
		break;
	}
	case STMT_SWITCH:
		check_switch(C, s);
		break;
	case STMT_RETURN: {
		Type *rt = C->fn->ret_type;
		Expr **vp = &s->as.ret.value;
		if (*vp) {
			Type *t = check_expr(C, vp, CTX_VALUE, rt);
			if (rt->kind == TY_VOID) {
				if (!type_is_error(t))
					sema_error((*vp)->loc, "'%s' returns void, so 'return' can't give a value", C->fn->name);
			} else {
				char what[160];
				snprintf(what, sizeof what, "the return value of '%s'", C->fn->name);
				coerce(C, vp, rt, what);
				Symbol *gone = address_of_local(*vp);
				if (gone)
					sema_warning((*vp)->loc, "this gives the address of '%s', which is gone once '%s' returns "
						     "(allocate the memory with addresses.allocate, or have the caller pass an address in)",
						     gone->name, C->fn->name);
			}
		} else if (rt->kind != TY_VOID && !type_is_error(rt)) {
			sema_error(s->loc, "'%s' must return a '%s' value here", C->fn->name, ts(rt));
			if (C->fn == C->S->main_fn)
				sema_note(s->loc, "write 'return 0' to report success");
		}
		break;
	}
	case STMT_BREAK:
		if (!C->breakables)
			sema_error(s->loc, "'break' must be inside a loop or switch");
		break;
	case STMT_CONTINUE:
		if (!C->loops)
			sema_error(s->loc, "'continue' must be inside a loop");
		break;
	case STMT_GOTO:
		record_goto(C, s);
		break;
	case STMT_LABEL:
		for (int i = 0; i < C->labels.len; i++)
			if (strcmp(C->labels.items[i]->as.label.name, s->as.label.name) == 0) {
				sema_error(s->loc, "label '%s' is defined twice", s->as.label.name);
				sema_note(C->labels.items[i]->loc, "first defined here");
			}
		s->as.label.block = C->bctx->block;
		s->as.label.index = C->bctx->index;
		vec_push(C->A, C->labels, s);
		break;
	}
}

static void check_block(Checker *C, Block *b)
{
	push_scope(C);
	b->parent = C->bctx ? C->bctx->block : NULL;
	BlockCtx ctx = { b, 0, C->bctx };
	C->bctx = &ctx;
	for (int i = 0; i < b->stmts.len; i++) {
		ctx.index = i;
		int mark = C->temps.len;
		Stmt *s = b->stmts.items[i];
		check_stmt(C, s);
		release_temps(C, mark);
		if (s->kind == STMT_DECL)
			for (int j = 0; j < s->as.decl.len; j++) {
				Symbol *sym = s->as.decl.items[j]->sym;
				if (sym && sym->kind != SYM_GLOBAL)
					vec_push(C->A, b->locals, sym);
			}
	}
	C->bctx = ctx.parent;
	pop_scope(C);
}

/* ====================================================== functions */

void check_function(Checker *C, FnDecl *fn)
{
	C->fn = fn;
	C->self_obj = fn->owner ? fn->owner->ty->obj : NULL;
	C->cur_file = fn->loc.file;
	C->scope = NULL;
	C->loop = NULL;
	C->pos = 0;
	C->acc.len = 0;
	C->loans.len = 0;
	C->temps.len = 0;
	C->binding = NULL;
	C->loops = C->breakables = 0;
	C->bctx = NULL;
	C->gotos.len = 0;
	C->labels.len = 0;
	C->last_use.len = 0;
	C->last_use.items = NULL;
	C->last_use.cap = 0;
	g_gotos.len = 0;
	g_gotos.items = NULL;
	g_gotos.cap = 0;
	fn->frame.len = 0;
	vec_push(C->A, C->S->functions, fn);

	int errors_before = diag_error_count();
	push_scope(C);
	if (fn->self_kind != SELF_NONE && fn->owner) {
		Type *st = type_ref(C->A, C->self_obj->type, fn->self_kind == SELF_MUT_REF);
		fn->self_sym = new_symbol(C, "self", SYM_SELF, st, false, fn->loc);
	}
	for (int i = 0; i < fn->params.len; i++) {
		VarDecl *p = fn->params.items[i];
		Type *t = p->type->resolved ? p->type->resolved : &t_error;
		Symbol *s = new_symbol(C, p->name, SYM_PARAM, t, p->is_const, p->loc);
		declare(C, s);
		p->sym = s;
	}
	check_block(C, fn->body);
	pop_scope(C);
	resolve_gotos(C);

	if (diag_error_count() == errors_before) {
		flow_function(C, fn);
		if (diag_error_count() == errors_before)
			check_loans(C, fn);
	}
	C->fn = NULL;
	C->self_obj = NULL;
}
