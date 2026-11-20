#include <string.h>
/* sema_flow.c — stage 4: move tracking and borrow (loan) conflicts. */
#include "sema_int.h"

/* ====================================================== move dataflow */

enum { ST_INIT, ST_MOVED, ST_MAYBE };

typedef struct {
	bool dead;              /* unreachable */
	unsigned char *st;
	SrcLoc *moved_at;
} FState;

typedef struct FLoop {
	struct FLoop *parent;
	bool is_loop;           /* false for a switch: only `break` targets it */
	FState brk, cont;
} FLoop;

typedef struct {
	Stmt *label;
	FState state;
} Pending;

typedef struct {
	Checker *C;
	FnDecl *fn;
	int n;
	FLoop *loop;
	VEC(SrcLoc) reported;
	VEC(Pending) pending;   /* forward gotos waiting for their label */
	bool warned_unreachable;
} Flow;

static FState fs_new(int n, bool dead)
{
	FState s;
	s.dead = dead;
	s.st = calloc((size_t)(n ? n : 1), 1);
	s.moved_at = calloc((size_t)(n ? n : 1), sizeof(SrcLoc));
	return s;
}

static FState fs_copy(const FState *src, int n)
{
	FState s = fs_new(n, src->dead);
	memcpy(s.st, src->st, (size_t)n);
	memcpy(s.moved_at, src->moved_at, sizeof(SrcLoc) * (size_t)n);
	return s;
}

static void fs_free(FState *s)
{
	free(s->st);
	free(s->moved_at);
	s->st = NULL;
	s->moved_at = NULL;
}

static void fs_assign(FState *dst, FState *src)
{
	fs_free(dst);
	*dst = *src;
}

static void fs_merge(FState *dst, const FState *src, int n)
{
	if (src->dead)
		return;
	if (dst->dead) {
		dst->dead = false;
		memcpy(dst->st, src->st, (size_t)n);
		memcpy(dst->moved_at, src->moved_at, sizeof(SrcLoc) * (size_t)n);
		return;
	}
	for (int i = 0; i < n; i++) {
		if (dst->st[i] != src->st[i])
			dst->st[i] = ST_MAYBE;
		if (!dst->moved_at[i].line)
			dst->moved_at[i] = src->moved_at[i];
	}
}

static bool fs_equal(const FState *a, const FState *b, int n)
{
	if (a->dead != b->dead)
		return false;
	return a->dead || memcmp(a->st, b->st, (size_t)n) == 0;
}

static bool first_report(Flow *F, SrcLoc loc)
{
	for (int i = 0; i < F->reported.len; i++)
		if (F->reported.items[i].line == loc.line && F->reported.items[i].col == loc.col &&
		    F->reported.items[i].file == loc.file)
			return false;
	vec_push(F->C->A, F->reported, loc);
	return true;
}

static void require_init(Flow *F, FState *s, Symbol *sym, SrcLoc loc)
{
	if (s->dead || sym->kind == SYM_GLOBAL)
		return;
	int st = s->st[sym->index];
	if (st == ST_INIT || type_is_error(sym->type) || !first_report(F, loc))
		return;
	SrcLoc at = s->moved_at[sym->index];
	if (st == ST_MOVED) {
		sema_error(loc, "use of moved value '%s'", sym->name);
		if (at.line)
			sema_note(at, "'%s' was moved here; a '%s' is moved, not copied", sym->name, ts(sym->type));
	} else {
		sema_error(loc, "'%s' may have been moved on some paths to here", sym->name);
		if (at.line)
			sema_note(at, "it may be moved here");
	}
}

static void flow_expr(Flow *F, Expr *e, FState *s);

static void flow_place_parts(Flow *F, Expr *e, FState *s)
{
	switch (e->kind) {
	case EXPR_FIELD: flow_expr(F, e->as.field.object, s); break;
	case EXPR_INDEX: flow_expr(F, e->as.index.base, s); flow_expr(F, e->as.index.index, s); break;
	default: break;
	}
}

static void flow_expr(Flow *F, Expr *e, FState *s)
{
	if (!e)
		return;
	switch (e->kind) {
	case EXPR_INT: case EXPR_FLOAT: case EXPR_BOOL: case EXPR_CHAR: case EXPR_STRING: case EXPR_SIZEOF:
	case EXPR_HANDLER:
	case EXPR_CALLBACK:
		break;
	case EXPR_AT:
		flow_expr(F, e->as.at.address, s);
		flow_expr(F, e->as.at.step, s);
		break;
	case EXPR_IDENT:
	case EXPR_SELF: {
		Symbol *sym = e->as.ident.sym;
		if (!sym || sym->kind == SYM_GLOBAL)
			break;
		require_init(F, s, sym, e->loc);
		if (e->use == USE_MOVE && !s->dead) {
			s->st[sym->index] = ST_MOVED;
			s->moved_at[sym->index] = e->loc;
		}
		break;
	}
	case EXPR_UNARY:  flow_expr(F, e->as.unary.operand, s); break;
	case EXPR_BORROW: flow_expr(F, e->as.borrow.operand, s); break;
	case EXPR_CAST:   flow_expr(F, e->as.cast.value, s); break;
	case EXPR_BINARY:
		flow_expr(F, e->as.binary.lhs, s);
		if (ast_binop_is_logical(e->as.binary.op)) {
			FState r = fs_copy(s, F->n);
			flow_expr(F, e->as.binary.rhs, &r);
			fs_merge(s, &r, F->n);
			fs_free(&r);
		} else {
			flow_expr(F, e->as.binary.rhs, s);
		}
		break;
	case EXPR_ASSIGN: {
		Expr *t = e->as.assign.target;
		flow_expr(F, e->as.assign.value, s);
		if (t->kind == EXPR_IDENT && t->as.ident.sym && t->as.ident.sym->kind != SYM_GLOBAL) {
			Symbol *sym = t->as.ident.sym;
			if (e->as.assign.compound || sym->type->kind == TY_REF)
				require_init(F, s, sym, t->loc);
			if (!s->dead)
				s->st[sym->index] = ST_INIT;
		} else {
			/* writing a field/element needs the containing value to exist */
			Expr *root = t;
			while (root->kind == EXPR_FIELD || root->kind == EXPR_INDEX)
				root = root->kind == EXPR_FIELD ? root->as.field.object : root->as.index.base;
			if ((root->kind == EXPR_IDENT || root->kind == EXPR_SELF) && root->as.ident.sym)
				require_init(F, s, root->as.ident.sym, root->loc);
			flow_place_parts(F, t, s);
		}
		break;
	}
	case EXPR_CALL: {
		for (int i = 0; i < e->as.call.args.len; i++)
			flow_expr(F, e->as.call.args.items[i], s);
		Expr *callee = e->as.call.callee;
		if (callee->kind == EXPR_FIELD && !e->as.call.is_static)
			flow_expr(F, callee->as.field.object, s);
		break;
	}
	case EXPR_FIELD: flow_expr(F, e->as.field.object, s); break;
	case EXPR_INDEX:
		flow_expr(F, e->as.index.base, s);
		flow_expr(F, e->as.index.index, s);
		break;
	case EXPR_INIT:
		for (int i = 0; i < e->as.init.entries.len; i++) {
			flow_expr(F, e->as.init.entries.items[i].key, s);
			flow_expr(F, e->as.init.entries.items[i].value, s);
		}
		break;
	}
}

static void flow_block(Flow *F, Block *b, FState *s);
static void flow_stmt(Flow *F, Stmt *st, FState *s);

/* Generic loop driver. Order per iteration: [cond] body [cont-merge] [update],
 * or for do-while: body [cont-merge] cond. Iterates to a fixed point. */
static void flow_loop(Flow *F, Expr *cond, Block *body, Stmt *update, bool cond_first, FState *s)
{
	int n = F->n;
	bool always = !cond || (cond->kind == EXPR_BOOL && cond->as.bool_val) ||
		      (cond->kind == EXPR_CAST && cond->as.cast.value->kind == EXPR_INT &&
		       cond->as.cast.value->as.int_lit.value != 0);
	FState head = fs_copy(s, n);
	FState exit_state = fs_new(n, true);

	for (int iter = 0; iter < 16; iter++) {
		FState it = fs_copy(&head, n);
		FState after_cond = fs_new(n, true);
		if (cond_first) {
			flow_expr(F, cond, &it);
			fs_free(&after_cond);
			after_cond = fs_copy(&it, n);
		}
		FLoop L = { F->loop, true, fs_new(n, true), fs_new(n, true) };
		F->loop = &L;
		flow_block(F, body, &it);
		F->loop = L.parent;
		fs_merge(&it, &L.cont, n);
		if (update)
			flow_stmt(F, update, &it);
		if (!cond_first) {
			flow_expr(F, cond, &it);
			fs_free(&after_cond);
			after_cond = fs_copy(&it, n);
		}

		FState next = fs_copy(&head, n);
		fs_merge(&next, &it, n);
		bool stable = fs_equal(&next, &head, n);

		fs_free(&exit_state);
		exit_state = always ? fs_new(n, true) : fs_copy(&after_cond, n);
		fs_merge(&exit_state, &L.brk, n);

		fs_free(&it);
		fs_free(&after_cond);
		fs_free(&L.brk);
		fs_free(&L.cont);
		fs_assign(&head, &next);
		if (stable)
			break;
	}
	fs_free(&head);
	fs_assign(s, &exit_state);
}

static void mark_decl(Flow *F, VarDecl *v, FState *s)
{
	if (!v->sym || v->sym->kind == SYM_GLOBAL)
		return;
	flow_expr(F, v->init, s);
	if (!s->dead) {
		s->st[v->sym->index] = ST_INIT;   /* every variable starts at a value (zero by default) */
		s->moved_at[v->sym->index] = (SrcLoc){ 0, 0, 0 };
	}
}

static void flow_stmt(Flow *F, Stmt *st, FState *s)
{
	int n = F->n;
	if (st->recover) {
		/* it panicking part way: what it did before then may be done, so
		 * the else part starts from where it ends (moves and all) */
		Recover *r = st->recover;
		st->recover = NULL;
		flow_stmt(F, st, s);
		st->recover = r;
		FState fail = fs_copy(s, n);
		if (r->value)
			flow_expr(F, r->value, &fail);
		if (r->block)
			flow_block(F, r->block, &fail);
		if (st->kind == STMT_DECL && !r->value && !fail.dead && !s->dead)
			sema_error(r->loc, "'%s' has no value if this fails, so the 'else' block has to leave: return, break or "
				   "continue (or write 'else Value' to give it one)", st->as.decl.items[0]->name);
		fs_merge(s, &fail, n);
		fs_free(&fail);
		return;
	}
	switch (st->kind) {
	case STMT_DECL:
		for (int i = 0; i < st->as.decl.len; i++)
			mark_decl(F, st->as.decl.items[i], s);
		break;
	case STMT_EXPR:
		flow_expr(F, st->as.expr, s);
		if (st->as.expr->kind == EXPR_CALL && st->as.expr->as.call.builtin == BUILTIN_EXIT)
			s->dead = true;   /* (exit: the program ends there) */
		break;
	case STMT_IF: {
		flow_expr(F, st->as.if_.cond, s);
		FState t = fs_copy(s, n);
		flow_block(F, st->as.if_.then_block, &t);
		if (st->as.if_.else_branch)
			flow_stmt(F, st->as.if_.else_branch, s);
		fs_merge(s, &t, n);
		fs_free(&t);
		break;
	}
	case STMT_BLOCK:
		flow_block(F, st->as.block, s);
		break;
	case STMT_WHILE:
		flow_loop(F, st->as.while_.cond, st->as.while_.body, NULL, true, s);
		break;
	case STMT_DO:
		flow_loop(F, st->as.while_.cond, st->as.while_.body, NULL, false, s);
		break;
	case STMT_FOR:
		if (st->as.for_.init)
			flow_stmt(F, st->as.for_.init, s);
		flow_loop(F, st->as.for_.cond, st->as.for_.body, st->as.for_.update, true, s);
		break;
	case STMT_SWITCH: {
		flow_expr(F, st->as.switch_.value, s);
		FState entry = fs_copy(s, n);
		FState fall = fs_new(n, true);
		FLoop L = { F->loop, false, fs_new(n, true), fs_new(n, true) };
		F->loop = &L;
		for (int i = 0; i < st->as.switch_.cases.len; i++) {
			FState cur = fs_copy(&entry, n);
			fs_merge(&cur, &fall, n);
			flow_block(F, st->as.switch_.cases.items[i].body, &cur);
			fs_assign(&fall, &cur);
		}
		F->loop = L.parent;
		FState out = fs_copy(&fall, n);
		fs_merge(&out, &L.brk, n);
		if (st->as.switch_.default_index < 0)
			fs_merge(&out, &entry, n);
		fs_assign(s, &out);
		fs_free(&entry);
		fs_free(&fall);
		fs_free(&L.brk);
		fs_free(&L.cont);
		break;
	}
	case STMT_RETURN:
		flow_expr(F, st->as.ret.value, s);
		s->dead = true;
		break;
	case STMT_BREAK:
		if (F->loop)
			fs_merge(&F->loop->brk, s, n);
		s->dead = true;
		break;
	case STMT_CONTINUE: {
		FLoop *L = F->loop;
		while (L && !L->is_loop)
			L = L->parent;
		if (L)
			fs_merge(&L->cont, s, n);
		s->dead = true;
		break;
	}
	case STMT_GOTO: {
		Stmt *target = st->as.goto_.target;
		bool found = false;
		for (int i = 0; i < F->pending.len; i++)
			if (F->pending.items[i].label == target) {
				fs_merge(&F->pending.items[i].state, s, n);
				found = true;
			}
		if (!found) {
			Pending p = { target, fs_copy(s, n) };
			vec_push(F->C->A, F->pending, p);
		}
		s->dead = true;
		break;
	}
	case STMT_LABEL:
		for (int i = 0; i < F->pending.len; i++)
			if (F->pending.items[i].label == st)
				fs_merge(s, &F->pending.items[i].state, n);
		break;
	}
}

static void flow_block(Flow *F, Block *b, FState *s)
{
	for (int i = 0; i < b->stmts.len; i++) {
		Stmt *st = b->stmts.items[i];
		if (s->dead && st->kind != STMT_LABEL) {
			if (!F->warned_unreachable) {
				sema_warning(st->loc, "this code can never run");
				F->warned_unreachable = true;
			}
			continue;   /* only a label can make the rest reachable again */
		}
		flow_stmt(F, st, s);
	}
}

void flow_function(Checker *C, FnDecl *fn)
{
	Flow F;
	memset(&F, 0, sizeof F);
	F.C = C;
	F.fn = fn;
	F.n = fn->frame.len;
	FState s = fs_new(F.n, false);   /* calloc: every slot ST_INIT (0) */
	flow_block(&F, fn->body, &s);
	if (!s.dead && fn->ret_type && fn->ret_type->kind != TY_VOID && !type_is_error(fn->ret_type) &&
	    fn != C->S->main_fn)
		sema_error(fn->loc, "'%s' can reach the end of its body without returning a '%s'", fn->name,
			   ts(fn->ret_type));
	for (int i = 0; i < F.pending.len; i++)
		fs_free(&F.pending.items[i].state);
	fs_free(&s);
}

/* ====================================================== borrow checking */

static const char *conflict_message(const Loan *l, AccessKind k, char *buf, size_t size)
{
	const char *n = l->root->name;
	if (!l->is_mut) {
		switch (k) {
		case ACC_WRITE:      snprintf(buf, size, "can't change '%s' while it is borrowed", n); break;
		case ACC_MOVE:       snprintf(buf, size, "can't move '%s' while it is borrowed", n); break;
		case ACC_BORROW_MUT: snprintf(buf, size, "can't lend '%s' as mutable while it is also borrowed read-only", n); break;
		default:             return NULL;
		}
	} else {
		switch (k) {
		case ACC_READ:       snprintf(buf, size, "can't use '%s' while it is lent as mutable", n); break;
		case ACC_WRITE:      snprintf(buf, size, "can't change '%s' while it is lent as mutable", n); break;
		case ACC_MOVE:       snprintf(buf, size, "can't move '%s' while it is lent as mutable", n); break;
		case ACC_BORROW:     snprintf(buf, size, "can't borrow '%s' while it is lent as mutable", n); break;
		case ACC_BORROW_MUT: snprintf(buf, size, "can't lend '%s' as mutable twice at the same time", n); break;
		}
	}
	return buf;
}

void check_loans(Checker *C, FnDecl *fn)
{
	for (int i = 0; i < fn->frame.len; i++) {
		Symbol *s = fn->frame.items[i];
		s->live_end = s->max_use;
		if (s->ext_loop && s->ext_loop->end > s->live_end)
			s->live_end = s->ext_loop->end;
	}
	for (bool changed = true; changed;) {
		changed = false;
		for (int i = 0; i < fn->frame.len; i++) {
			Symbol *s = fn->frame.items[i];
			for (int j = 0; j < s->sources.len; j++) {
				Symbol *src = s->sources.items[j];
				if (src->live_end < s->live_end) {
					src->live_end = s->live_end;
					changed = true;
				}
			}
		}
	}
	for (int i = 0; i < C->loans.len; i++) {
		Loan *l = &C->loans.items[i];
		if (l->borrower)
			l->end = l->borrower->live_end;
		if (l->end < l->start)
			l->end = l->start;
		if (l->loop) {
			if (l->loop->start < l->start)
				l->start = l->loop->start;
			if (l->loop->end > l->end)
				l->end = l->loop->end;
		}
	}
	VEC(int) reported = { 0 };
	for (int i = 0; i < C->loans.len; i++) {
		Loan *l = &C->loans.items[i];
		for (int j = 0; j < C->acc.len; j++) {
			Access *a = &C->acc.items[j];
			if (a->sym != l->root || a->pos == l->created - 1 || a->pos < l->start || a->pos > l->end ||
			    !paths_overlap(a->path, l->path))
				continue;
			if (a->pos <= l->created && !l->loop)
				continue;
			char msg[256];
			if (!conflict_message(l, a->kind, msg, sizeof msg))
				continue;
			bool dup = false;
			for (int k = 0; k < reported.len && !dup; k++)
				dup = reported.items[k] == j;
			if (dup)
				continue;
			vec_push(C->A, reported, j);
			if (l->borrower && strcmp(l->borrower->name, " seq") == 0) {
				/* the hidden borrow of a `for X in Things` loop */
				sema_error(a->loc, "can't change '%s' inside a loop that goes through it", l->root->name);
				bool lk = l->borrower->type && l->borrower->type->kind == TY_REF && l->borrower->type->inner &&
					  l->borrower->type->inner->kind == TY_LOOKUP;
				sema_note(l->loc, "the loop over '%s' starts here; collect the changes in another %s "
					  "and make them after the loop", l->root->name, lk ? "lookup (or a list)" : "list");
				continue;
			}
			sema_error(a->loc, "%s", msg);
			sema_note(l->loc, "'%s' is %s here", l->root->name, l->is_mut ? "lent as mutable" : "borrowed");
			if (l->borrower && l->borrower->index < C->last_use.len) {
				SrcLoc u = C->last_use.items[l->borrower->index];
				if (u.line && (u.line > a->loc.line || (u.line == a->loc.line && u.col > a->loc.col) || l->loop))
					sema_note(u, "the borrow is still in use here, through '%s'", l->borrower->name);
			}
		}
	}
}
