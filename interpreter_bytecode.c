/* interpreter_bytecode.c — the interpreter's bytecode: each function made
 * into a list of simple instructions the first time it's called, and run
 * from that list after, rather than by walking its syntax tree.
 *
 * Included into interpreter.c (it uses the tree-walker's own parts), just
 * before call_function. A function's variables are the same cells the
 * tree-walker uses, so the two work together: what the bytecode can't do
 * yet, a whole statement or a part of an expression, it hands to the
 * tree-walker (BC_STMT, BC_EVAL), and that runs on the same cells. So a
 * program runs the same either way, and the bytecode grows a piece at a
 * time. CABBY_BYTECODE=0 turns it off, to compare.
 *
 * For now it does numbers: whole numbers, decimals and true or false, in
 * a function's own variables; arithmetic, comparisons, `if`, the loops,
 * and calls to functions. Everything is worked out as the tree-walker
 * works it out (arith, compare, convert), so overflow, wrapping, division
 * by zero and their panics are the same. */

/* ------------------------------------------------------------ instructions */

enum {
	BC_CONST,        /* R[a] = v */
	BC_MOV,          /* R[a] = R[b] */
	BC_ADD_I, BC_SUB_I, BC_MUL_I,           /* R[a] = R[b] op R[c], whole numbers of type p */
	BC_ADD_F, BC_SUB_F, BC_MUL_F, BC_DIV_F, /* decimals (doubles) */
	BC_ARITH,        /* R[a] = R[b] op R[c]: arith, for Expr p */
	BC_DIVMOD_I,     /* R[a] = R[b] / or % R[c] (whole numbers, Expr p): arith for 0 and -1 */
	BC_FMOD,         /* R[a] = fmod(R[b], R[c]) */
	BC_LT_I, BC_LE_I, BC_GT_I, BC_GE_I, BC_EQ_I, BC_NE_I,   /* signed whole numbers (and chars) */
	BC_CMP,          /* R[a] = R[b] op R[c]: any plain type, for Expr p (its operator, lhs type) */
	BC_NEG_I, BC_NEG_F, BC_NOT, BC_BITNOT,  /* R[a] = op R[b] (type p) */
	BC_CONVERT,      /* R[a] = convert(R[b], from p, to q) */
	BC_WRAP_I,       /* R[a] = R[b], a whole number made the size k */
	BC_JMP,          /* to a */
	BC_JF, BC_JT,    /* to a if R[b] is false / true */
	BC_JF_LT_I, BC_JF_LE_I, BC_JF_GT_I, BC_JF_GE_I, BC_JF_EQ_I, BC_JF_NE_I,   /* to a unless R[b] op R[c] */
	BC_JEQ_K,        /* to a if R[b] is the whole number v (a switch's case) */
	BC_INIT,         /* R[a] = the object q (an EXPR_INIT) made, its fields given from R[b], R[b+1]... */
	BC_CALL,         /* R[a] = fn p (R[b] ... R[b + c - 1]), at Expr q */
	BC_EVAL,         /* R[a] = the tree-walker's value of Expr p */
	BC_STMT,         /* the tree-walker runs Stmt p; then to a, b, c on return, break, continue */
	BC_COMPOUND,     /* R[a] op= R[b], for the assignment Expr p */
	BC_ADD_TO_I,     /* R[a] op= R[b] (op c: +, - or *), whole numbers of type p */
	BC_ADDK_TO_I,    /* R[a] op= v, the same, by a number the instruction carries */
	BC_DROP,         /* the locals of Block p, left (a: some aren't numbers) */
	BC_TICK,         /* each time round a loop: Ctrl-C, and other threads' turns */
	BC_SETRET,       /* the function's answer is R[a] */
	BC_RET,
	BC_INDEX_GET,    /* R[a] = element R[b] of the array or list in Symbol p, for Expr q */
	BC_INDEX_SET,    /* element R[a] of the array or list in Symbol p = R[b], for the target Expr q */
	BC_INDEX_COMPOUND,   /* element R[a] op= R[b], for the assignment Expr q */
	BC_CALLM,        /* R[a] = method p of the receiver in Symbol r (R[b] ... R[b + c - 1]), at Expr q */
	BC_DROPVAL,      /* R[a], a call's answer nobody wants, let go of */
	BC_FIELD_GET,    /* R[a] = the field (Expr q) of the object in Symbol p */
	BC_FIELD_SET,    /* the field (target Expr q) of the object in Symbol p = R[b] */
	BC_FIELD_COMPOUND,   /* the field op= R[b], for the assignment Expr q */
	BC_LEN,          /* R[a] = the length of the string (k 0), list (1) or array (2) in Symbol p */
	BC_STR_GET,      /* R[a] = character R[b] of the string in Symbol p, for Expr q */
	BC_LK_HAS,       /* R[a] = whether the lookup in Symbol p has key R[b] */
	BC_LK_GET,       /* R[a] = the value for key R[b] of the lookup in Symbol p, for Expr q */
	BC_LK_PUT,       /* the lookup in Symbol p's value for key R[a] = R[b] (added if new) */
	BC_LK_COMPOUND,  /* the value for key R[a] op= R[b], for the assignment Expr q */
	BC_LK_PART,      /* R[a] = the key (c 1) or value (2) at position R[b] of the lookup in Symbol p */
	BC_LK_NEXT,      /* R[a] = the next position in use at or after R[b] in the lookup in Symbol p, or -1 */
	BC_ZERO,         /* R[a] = a new, empty value of Type p (a string, a list...) */
	BC_LIST_ADD,     /* R[a] added to the end of the list of numbers in Symbol p, for Expr q */
	BC_SLICE,        /* R[a] = the string in Symbol r, from R[b] to R[c] (the slice Expr q's flags) */
	BC_LK_REMOVE,    /* the key R[b] (or, c 1, the variable r) taken out of the lookup in Symbol p, for Expr q */
	BC_STRB,         /* R[a] = the builtin call Expr p (s.trim, L.contains x), by the tree-walker's code for it */
	BC_MARK,         /* R[a] = how many temporaries there are now */
	BC_RELEASE,      /* the temporaries made since BC_MARK a let go of (a statement's end) */
	BC_NOP,          /* (taken out when the function's made) */
	BC_COUNT
};

typedef struct {
	int op;
	int a, b, c;
	int k;           /* whole numbers: their size, to wrap to (bc_wrap) */
	Value v;
	void *p, *q;
	void *r;         /* BC_CALL: the callee's bytecode, once known */
} BcIns;

typedef struct Bytecode {
	BcIns *code;
	int n, cap;
	int ntemps;              /* cells after the function's own, for workings */
	struct Bytecode *next_made;   /* all of them, to let go of at the end */
	bool quick;              /* every variable a number, and not a library's: called by bc_quick_call */
} Bytecode;

static Bytecode *g_bc_made;

/* ------------------------------------------------------------ what can be done */

static bool g_bc_off;        /* CABBY_BYTECODE=0, or the interactive session */

static bool bc_plain(const Type *t)
{
	return t && (t->kind == TY_INT || t->kind == TY_FLOAT || t->kind == TY_BOOL);
}

/* a function's own variable holding a number: its cell is the register */
static bool bc_plain_local(const Symbol *s)
{
	return s && s->kind != SYM_GLOBAL && bc_plain(s->type);
}

/* Not made into bytecode at all: functions with a goto (the tree-walker's
 * blocks jump about), or that lend out one of their own numbers (&N: a
 * call could change it part way through an expression) */
static bool bc_refuse_expr(Expr *e);
static bool bc_refuse_stmt(Stmt *s);

static bool bc_refuse_block(Block *b)
{
	for (int i = 0; b && i < b->stmts.len; i++)
		if (bc_refuse_stmt(b->stmts.items[i]))
			return true;
	return false;
}

static bool bc_refuse_expr(Expr *e)
{
	if (!e)
		return false;
	switch (e->kind) {
	case EXPR_BORROW:
		if (e->as.borrow.operand->kind == EXPR_IDENT && bc_plain_local(e->as.borrow.operand->as.ident.sym))
			return true;
		return bc_refuse_expr(e->as.borrow.operand);
	case EXPR_AT:     return bc_refuse_expr(e->as.at.address) || bc_refuse_expr(e->as.at.step);
	case EXPR_UNARY:  return bc_refuse_expr(e->as.unary.operand);
	case EXPR_CAST:   return bc_refuse_expr(e->as.cast.value);
	case EXPR_BINARY: return bc_refuse_expr(e->as.binary.lhs) || bc_refuse_expr(e->as.binary.rhs);
	case EXPR_ASSIGN: return bc_refuse_expr(e->as.assign.target) || bc_refuse_expr(e->as.assign.value);
	case EXPR_CALL:
		for (int i = 0; i < e->as.call.args.len; i++)
			if (bc_refuse_expr(e->as.call.args.items[i]))
				return true;
		return e->as.call.callee->kind == EXPR_FIELD && bc_refuse_expr(e->as.call.callee->as.field.object);
	case EXPR_FIELD:  return bc_refuse_expr(e->as.field.object);
	case EXPR_INDEX:
		return bc_refuse_expr(e->as.index.base) || bc_refuse_expr(e->as.index.index) || bc_refuse_expr(e->as.index.to);
	case EXPR_INIT:
		for (int i = 0; i < e->as.init.entries.len; i++)
			if (bc_refuse_expr(e->as.init.entries.items[i].key) || bc_refuse_expr(e->as.init.entries.items[i].value))
				return true;
		return false;
	default:
		return false;
	}
}

static bool bc_refuse_stmt(Stmt *s)
{
	if (!s)
		return false;
	if (s->recover)
		return true;   /* (`... else`: the tree-walker catches its panic) */
	switch (s->kind) {
	case STMT_DECL:
		for (int i = 0; i < s->as.decl.len; i++)
			if (bc_refuse_expr(s->as.decl.items[i]->init))
				return true;
		return false;
	case STMT_EXPR:   return bc_refuse_expr(s->as.expr);
	case STMT_IF:
		return bc_refuse_expr(s->as.if_.cond) || bc_refuse_block(s->as.if_.then_block) ||
		       bc_refuse_stmt(s->as.if_.else_branch);
	case STMT_BLOCK:  return bc_refuse_block(s->as.block);
	case STMT_WHILE:
	case STMT_DO:     return bc_refuse_expr(s->as.while_.cond) || bc_refuse_block(s->as.while_.body);
	case STMT_FOR:
		return bc_refuse_stmt(s->as.for_.init) || bc_refuse_expr(s->as.for_.cond) ||
		       bc_refuse_stmt(s->as.for_.update) || bc_refuse_block(s->as.for_.body);
	case STMT_SWITCH:
		if (bc_refuse_expr(s->as.switch_.value))
			return true;
		for (int i = 0; i < s->as.switch_.cases.len; i++)
			if (bc_refuse_block(s->as.switch_.cases.items[i].body))
				return true;
		return false;
	case STMT_RETURN: return bc_refuse_expr(s->as.ret.value);
	case STMT_GOTO:
	case STMT_LABEL:  return true;
	default:          return false;
	}
}

/* ------------------------------------------------------------ the compiler */

typedef struct {
	int block_depth;         /* blocks open when the loop began (its body's not counted) */
	int *breaks, nbreaks;    /* jumps to patch: to after the loop */
	int *conts, nconts;      /* and to where it goes round again */
	bool is_switch;          /* a switch: break leaves it, continue is the loop's around it */
} BcLoop;

typedef struct {
	Bytecode *bc;
	FnDecl *fn;
	int base;                /* the first working cell: after the function's own */
	int next, max;           /* workings in use, and the most at once */
	Block *blocks[256];
	int nblocks;
	BcLoop *loops[64];
	int nloops;
	int evals;               /* BC_EVALs so far: whether a statement made temporaries */
	bool fail;
} BcComp;

static int bc_emit(BcComp *C, int op, int a, int b, int c, void *p, void *q)
{
	Bytecode *bc = C->bc;
	if (bc->n == bc->cap) {
		bc->cap = bc->cap ? bc->cap * 2 : 64;
		bc->code = rt_realloc(bc->code, sizeof *bc->code * (size_t)bc->cap);
	}
	bc->code[bc->n] = (BcIns){ op, a, b, c, 0, { 0 }, p, q, NULL };
	return bc->n++;
}

static int bc_here(BcComp *C) { return C->bc->n; }
static void bc_patch(BcComp *C, int at, int to) { C->bc->code[at].a = to; }

static int bc_temp(BcComp *C)
{
	int r = C->base + C->next++;
	if (C->next > C->max)
		C->max = C->next;
	return r;
}

static int bc_expr(BcComp *C, Expr *e);

/* something whose working could change a variable (a call, or the
 * tree-walker): what's read before it is read into a working first */
static bool bc_calm(Expr *e)
{
	switch (e->kind) {
	case EXPR_INT: case EXPR_FLOAT: case EXPR_BOOL: case EXPR_CHAR: case EXPR_IDENT: case EXPR_SELF:
		return true;
	case EXPR_UNARY:  return bc_calm(e->as.unary.operand);
	case EXPR_CAST:   return bc_calm(e->as.cast.value);
	case EXPR_BINARY: return bc_calm(e->as.binary.lhs) && bc_calm(e->as.binary.rhs);
	default:          return false;
	}
}

/* one of the string, list and lookup builtins (s.trim, L.contains x):
 * the tree-walker's own code for them, called straight */
static bool bc_str_builtin(const Expr *e)
{
	return e->kind == EXPR_CALL && e->as.call.builtin > BUILTIN_PRINTLN;
}

/* the tree-walker works out e (a number) */
static int bc_eval(BcComp *C, Expr *e)
{
	int r = bc_temp(C);
	bc_emit(C, bc_str_builtin(e) ? BC_STRB : BC_EVAL, r, 0, 0, e, NULL);
	C->evals++;
	return r;
}

/* Temporaries the tree-walker makes (a string, an object that's made and
 * then asked something) are let go of at the end of the statement, newest
 * first, as the tree-walker does: so a statement starts with a mark, and
 * ends by letting go back to it, unless nothing was made */
typedef struct {
	int at, reg, evals;
} BcMark;

static BcMark bc_mark(BcComp *C)
{
	BcMark m = { 0, bc_temp(C), C->evals };
	m.at = bc_emit(C, BC_MARK, m.reg, 0, 0, NULL, NULL);
	return m;
}

static void bc_release(BcComp *C, BcMark m)
{
	if (C->evals == m.evals)
		C->bc->code[m.at].op = BC_NOP;
	else
		bc_emit(C, BC_RELEASE, m.reg, 0, 0, NULL, NULL);
}

/* the receiver of a method call, if it's a variable (or self): the place
 * it's lent from */
static Symbol *bc_receiver(Expr *e)
{
	Expr *callee = e->as.call.callee;
	if (callee->kind != EXPR_FIELD || e->as.call.is_static || e->as.call.worker_check)
		return NULL;
	Expr *r = callee->as.field.object;
	if ((r->kind == EXPR_IDENT || r->kind == EXPR_SELF) && r->as.ident.sym)
		return r->as.ident.sym;
	return NULL;
}

/* a call of one of the program's functions or methods (not C's, nor a
 * builtin): its arguments worked out here, numbers by the bytecode and
 * the rest by the tree-walker, as it would (moved, or lent) */
static bool bc_direct_call(Expr *e)
{
	if (e->as.call.builtin)
		return false;
	FnDecl *fn = e->as.call.fn;
	if (!fn)
		return false;
	if (fn->definition)
		fn = fn->definition;
	if (fn->is_extern || !fn->body || fn->params.len != e->as.call.args.len)
		return false;
	if (fn->self_sym)
		return bc_receiver(e) != NULL;
	return e->as.call.callee->kind == EXPR_IDENT && !e->as.call.is_static;
}

static int bc_call(BcComp *C, Expr *e)
{
	FnDecl *fn = e->as.call.fn->definition ? e->as.call.fn->definition : e->as.call.fn;
	int n = e->as.call.args.len;
	int mark = C->next;
	int first = C->base + C->next;
	for (int i = 0; i < n; i++)
		bc_temp(C);
	for (int i = 0; i < n; i++) {
		int at = C->next;
		Expr *a = e->as.call.args.items[i];
		int r = bc_plain(a->ty) ? bc_expr(C, a) : bc_eval(C, a);
		bc_emit(C, BC_MOV, first + i, r, 0, NULL, NULL);
		C->next = at;
	}
	C->next = mark;
	int dst = bc_temp(C);
	if (fn->self_sym) {
		int at = bc_emit(C, BC_CALLM, dst, first, n, fn, e);
		C->bc->code[at].r = bc_receiver(e);
	} else {
		bc_emit(C, BC_CALL, dst, first, n, fn, e);
	}
	return dst;
}

/* a whole number's size, for bc_wrap: 64 (nothing to do), the bits for a
 * signed one, minus the bits for an unsigned one */
static int bc_size(const Type *t)
{
	if (t->bits == 64)
		return 64;
	return t->is_signed ? t->bits : -t->bits;
}

/* as norm does: wrapped round to the number's size */
static inline int64_t bc_wrap(int64_t v, int k)
{
	if (k == 64)
		return v;
	if (k > 0)
		return (int64_t)((uint64_t)v << (64 - k)) >> (64 - k);
	return (int64_t)((uint64_t)v & (((uint64_t)1 << -k) - 1));
}

static bool bc_signed_int(const Type *t)
{
	return t->kind == TY_INT && t->is_signed;
}

static int bc_binary(BcComp *C, Expr *e)
{
	BinaryOp op = e->as.binary.op;
	Expr *l = e->as.binary.lhs, *r = e->as.binary.rhs;
	if (op == BIN_LOG_AND || op == BIN_LOG_OR) {
		int dst = bc_temp(C);
		int a = bc_expr(C, l);
		bc_emit(C, BC_MOV, dst, a, 0, NULL, NULL);
		int jump = bc_emit(C, op == BIN_LOG_AND ? BC_JF : BC_JT, 0, dst, 0, NULL, NULL);
		int b = bc_expr(C, r);
		bc_emit(C, BC_MOV, dst, b, 0, NULL, NULL);
		bc_patch(C, jump, bc_here(C));
		return dst;
	}
	bool cmp = ast_binop_is_comparison(op);
	if ((cmp && !bc_plain(l->ty)) || (!cmp && !bc_plain(e->ty)) || !bc_plain(r->ty))
		return bc_eval(C, e);
	int a = bc_expr(C, l);
	if (!bc_calm(r) && a < C->base) {   /* read now, as the tree-walker does */
		int t = bc_temp(C);
		bc_emit(C, BC_MOV, t, a, 0, NULL, NULL);
		a = t;
	}
	int b = bc_expr(C, r);
	int dst = bc_temp(C);
	if (cmp) {
		if (bc_signed_int(l->ty) && bc_signed_int(r->ty)) {
			int k = op == BIN_LT ? BC_LT_I : op == BIN_LE ? BC_LE_I : op == BIN_GT ? BC_GT_I :
				op == BIN_GE ? BC_GE_I : op == BIN_EQ ? BC_EQ_I : BC_NE_I;
			bc_emit(C, k, dst, a, b, NULL, NULL);
		} else {
			bc_emit(C, BC_CMP, dst, a, b, e, NULL);
		}
		return dst;
	}
	const Type *t = e->ty;
	if (t->kind == TY_FLOAT && t->bits == 64 && op != BIN_MOD) {
		int k = op == BIN_ADD ? BC_ADD_F : op == BIN_SUB ? BC_SUB_F : op == BIN_MUL ? BC_MUL_F : BC_DIV_F;
		bc_emit(C, k, dst, a, b, NULL, NULL);
	} else if (t->kind == TY_FLOAT && op == BIN_MOD) {
		bc_emit(C, BC_FMOD, dst, a, b, e, NULL);
	} else if (t->kind == TY_INT && (op == BIN_ADD || op == BIN_SUB || op == BIN_MUL)) {
		int at = bc_emit(C, op == BIN_ADD ? BC_ADD_I : op == BIN_SUB ? BC_SUB_I : BC_MUL_I, dst, a, b, (void *)t, NULL);
		C->bc->code[at].k = bc_size(t);
	} else if (t->kind == TY_INT && (op == BIN_DIV || op == BIN_MOD)) {
		int at = bc_emit(C, BC_DIVMOD_I, dst, a, b, e, NULL);
		C->bc->code[at].k = bc_size(t);
	} else {
		bc_emit(C, BC_ARITH, dst, a, b, e, NULL);
	}
	return dst;
}

/* a variable (or self), any type, as the thing something's of */
static Symbol *bc_variable(Expr *e)
{
	if ((e->kind == EXPR_IDENT || e->kind == EXPR_SELF) && e->as.ident.sym)
		return e->as.ident.sym;
	return NULL;
}

/* `s.length`, `L.length`, `xs.length`, `K.length` of a variable: its symbol */
static Symbol *bc_length(Expr *e)
{
	int b = e->as.call.builtin;
	if ((b != BUILTIN_STR_LEN && b != BUILTIN_LIST_LEN && b != BUILTIN_SLICE_LEN && b != BUILTIN_LOOKUP_LEN) ||
	    e->as.call.args.len != 1)
		return NULL;
	return bc_variable(e->as.call.args.items[0]);
}

/* a lookup of numbers to numbers, in a variable */
static bool bc_number_lookup(Expr *e)
{
	if (!bc_variable(e))
		return false;
	const Type *t = e->ty;
	while (t && t->kind == TY_REF)
		t = t->inner;
	return t && t->kind == TY_LOOKUP && bc_plain(t->key) && bc_plain(t->inner);
}

/* a lookup of anything to numbers, in a variable */
static bool bc_numbers_lookup(Expr *e)
{
	if (!bc_variable(e))
		return false;
	const Type *t = e->ty;
	while (t && t->kind == TY_REF)
		t = t->inner;
	return t && t->kind == TY_LOOKUP && bc_plain(t->inner);
}

/* `Counts[K]`, `Counts[K] = 1`: an element of a lookup of numbers, by a
 * key that's a number, or a variable (`Words[W]`) */
static bool bc_lookup_element(Expr *e)
{
	if (e->kind != EXPR_INDEX || e->as.index.is_slice || e->as.index.on_str || e->as.index.lookup_part)
		return false;
	Expr *k = e->as.index.index;
	return (bc_number_lookup(e->as.index.base) && bc_plain(k->ty)) ||
	       (bc_numbers_lookup(e->as.index.base) && !bc_plain(k->ty) && bc_variable(k));
}

/* a literal whole number or decimal, as the value it is: an instruction can
 * carry it (in v), rather than another putting it in a working first */
static bool bc_literal(Expr *e, Value *v)
{
	if (e->kind == EXPR_INT && bc_plain(e->ty)) {
		*v = v_int(norm((int64_t)e->as.int_lit.value, e->ty));
		return e->ty->kind == TY_INT;
	}
	return false;
}

/* the key of a lookup element: into a working (a number), or the variable
 * it's in (r of the instruction, c 1) */
static int bc_key(BcComp *C, Expr *k, Symbol **in)
{
	*in = NULL;
	if (!bc_plain(k->ty)) {
		*in = bc_variable(k);
		return 0;
	}
	return bc_expr(C, k);
}

static int bc_emit_keyed(BcComp *C, int op, int a, int b, Symbol *key, void *p, void *q)
{
	int at = bc_emit(C, op, a, b, key ? 1 : 0, p, q);
	C->bc->code[at].r = key;
	return at;
}

/* a field holding a number, of an object (or struct) in a variable: `Size`,
 * `S.Size` (not a union's, whose fields share their bytes, nor a worker's
 * from outside it, which is checked) */
static bool bc_field(Expr *e)
{
	if (e->kind != EXPR_FIELD || !bc_plain(e->ty) || e->as.field.worker_check || !bc_variable(e->as.field.object))
		return false;
	const Type *t = e->as.field.object->ty;
	while (t && t->kind == TY_REF)
		t = t->inner;
	return t && t->kind == TY_OBJECT && t->obj && t->obj->kind != OBJ_UNION;
}

/* an element of an array or list of numbers, in a variable: `Counts[i]` */
static bool bc_element(Expr *e)
{
	if (e->kind != EXPR_INDEX || e->as.index.on_str || e->as.index.is_slice || e->as.index.lookup_part ||
	    e->as.index.lookup_put || !bc_plain(e->ty) || !bc_plain(e->as.index.index->ty))
		return false;
	Expr *b = e->as.index.base;
	if (b->kind != EXPR_IDENT || !b->as.ident.sym)
		return false;
	const Type *t = b->as.ident.sym->type;
	if (t->kind == TY_REF)
		t = t->inner;   /* (a slice, `int a[]`, is an array lent: its own kind) */
	return t && (t->kind == TY_ARRAY || t->kind == TY_LIST || t->kind == TY_SLICE) && bc_plain(t->inner);
}

/* e (a number) into a cell: its own, for a variable; else a working */
static int bc_expr(BcComp *C, Expr *e)
{
	if (!bc_plain(e->ty))
		return bc_eval(C, e);
	switch (e->kind) {
	case EXPR_INT: case EXPR_FLOAT: case EXPR_BOOL: case EXPR_CHAR: {
		int r = bc_temp(C);
		int at = bc_emit(C, BC_CONST, r, 0, 0, NULL, NULL);
		Value v;
		if (e->kind == EXPR_INT)
			v = v_int(norm((int64_t)e->as.int_lit.value, e->ty));
		else if (e->kind == EXPR_FLOAT)
			v = v_float(e->ty->bits == 32 ? (double)(float)e->as.float_lit.value : e->as.float_lit.value);
		else if (e->kind == EXPR_BOOL)
			v = v_bool(e->as.bool_val);
		else
			v = v_int((int8_t)e->as.char_val);
		C->bc->code[at].v = v;
		return r;
	}
	case EXPR_IDENT: {
		Symbol *s = e->as.ident.sym;
		if (bc_plain_local(s))
			return s->index;
		return bc_eval(C, e);
	}
	case EXPR_UNARY: {
		Expr *o = e->as.unary.operand;
		if (e->as.unary.op == UN_NEG && o->kind == EXPR_INT) {
			int r = bc_temp(C);
			int at = bc_emit(C, BC_CONST, r, 0, 0, NULL, NULL);
			C->bc->code[at].v = v_int(norm((int64_t)(0 - o->as.int_lit.value), e->ty));
			return r;
		}
		if (!bc_plain(o->ty))
			return bc_eval(C, e);
		int a = bc_expr(C, o);
		int r = bc_temp(C);
		int k = e->as.unary.op == UN_NOT ? BC_NOT : e->as.unary.op == UN_BITNOT ? BC_BITNOT :
			o->ty->kind == TY_FLOAT ? BC_NEG_F : BC_NEG_I;
		bc_emit(C, k, r, a, 0, (void *)e->ty, NULL);
		return r;
	}
	case EXPR_BINARY:
		return bc_binary(C, e);
	case EXPR_CAST: {
		Expr *v = e->as.cast.value;
		if (!bc_plain(v->ty))
			return bc_eval(C, e);
		int a = bc_expr(C, v);
		if (v->ty == e->ty || type_eq(v->ty, e->ty))
			return a;   /* (convert does nothing) */
		int r = bc_temp(C);
		if (v->ty->kind == TY_INT && e->ty->kind == TY_INT) {
			int at = bc_emit(C, BC_WRAP_I, r, a, 0, NULL, NULL);   /* one whole number to another: norm */
			C->bc->code[at].k = bc_size(e->ty);
		} else {
			bc_emit(C, BC_CONVERT, r, a, 0, (void *)v->ty, (void *)e->ty);
		}
		return r;
	}
	case EXPR_CALL:
		if (bc_direct_call(e))
			return bc_call(C, e);
		if (bc_length(e)) {
			int r = bc_temp(C);
			int at = bc_emit(C, BC_LEN, r, 0, 0, bc_length(e), NULL);
			int b = e->as.call.builtin;
			C->bc->code[at].k = b == BUILTIN_STR_LEN ? 0 : b == BUILTIN_LIST_LEN ? 1 : b == BUILTIN_SLICE_LEN ? 2 : 3;
			return r;
		}
		if (e->as.call.builtin == BUILTIN_LOOKUP_CONTAINS && e->as.call.args.len == 2 &&
		    bc_numbers_lookup(e->as.call.args.items[0]) && !bc_plain(e->as.call.args.items[1]->ty) &&
		    bc_variable(e->as.call.args.items[1])) {
			int r = bc_temp(C);   /* (a key in a variable: `Words.contains W`) */
			bc_emit_keyed(C, BC_LK_HAS, r, 0, bc_variable(e->as.call.args.items[1]),
				      bc_variable(e->as.call.args.items[0]), e);
			return r;
		}
		if ((e->as.call.builtin == BUILTIN_LOOKUP_CONTAINS || e->as.call.builtin == BUILTIN_LOOKUP_NEXT) &&
		    e->as.call.args.len == 2 && bc_number_lookup(e->as.call.args.items[0]) &&
		    bc_plain(e->as.call.args.items[1]->ty)) {
			int k = bc_expr(C, e->as.call.args.items[1]);
			int r = bc_temp(C);
			bc_emit(C, e->as.call.builtin == BUILTIN_LOOKUP_CONTAINS ? BC_LK_HAS : BC_LK_NEXT, r, k, 0,
				bc_variable(e->as.call.args.items[0]), e);
			return r;
		}
		if (e->as.call.builtin == BUILTIN_STR_GET && bc_variable(e->as.call.args.items[0]) &&
		    bc_plain(e->as.call.args.items[1]->ty)) {
			int i = bc_expr(C, e->as.call.args.items[1]);
			int r = bc_temp(C);
			bc_emit(C, BC_STR_GET, r, i, 0, bc_variable(e->as.call.args.items[0]), e);
			return r;
		}
		return bc_eval(C, e);
	case EXPR_FIELD:
		if (bc_field(e)) {
			int r = bc_temp(C);
			bc_emit(C, BC_FIELD_GET, r, 0, 0, bc_variable(e->as.field.object), e);
			return r;
		}
		return bc_eval(C, e);
	case EXPR_INDEX: {
		if (bc_lookup_element(e)) {
			Symbol *ks;
			int k = bc_key(C, e->as.index.index, &ks);
			int r = bc_temp(C);
			bc_emit_keyed(C, BC_LK_GET, r, k, ks, bc_variable(e->as.index.base), e);
			return r;
		}
		if (e->as.index.lookup_part && bc_variable(e->as.index.base) && bc_plain(e->as.index.index->ty)) {
			int i = bc_expr(C, e->as.index.index);
			int r = bc_temp(C);
			bc_emit(C, BC_LK_PART, r, i, e->as.index.lookup_part, bc_variable(e->as.index.base), e);
			return r;
		}
		if (!bc_element(e))
			return bc_eval(C, e);
		int i = bc_expr(C, e->as.index.index);
		int r = bc_temp(C);
		bc_emit(C, BC_INDEX_GET, r, i, 0, e->as.index.base->as.ident.sym, e);
		return r;
	}
	default:
		return bc_eval(C, e);
	}
}

/* a condition: jump to (patched later) when it's false; fused for a
 * comparison of whole numbers */
static int bc_jump_on(BcComp *C, Expr *cond, bool when);

static int bc_jump_unless(BcComp *C, Expr *cond) { return bc_jump_on(C, cond, false); }

/* a jump (to be patched) when cond is true: at the bottom of a loop, back
 * round, so a turn is one jump, not a jump back and a test */
static int bc_jump_if(BcComp *C, Expr *cond) { return bc_jump_on(C, cond, true); }

static int bc_jump_on(BcComp *C, Expr *cond, bool when)
{
	int mark = C->next;
	BcMark m = bc_mark(C);
	if (cond->kind == EXPR_BINARY && ast_binop_is_comparison(cond->as.binary.op) &&
	    bc_signed_int(cond->as.binary.lhs->ty) && bc_signed_int(cond->as.binary.rhs->ty)) {
		Expr *l = cond->as.binary.lhs, *r = cond->as.binary.rhs;
		int a = bc_expr(C, l);
		if (!bc_calm(r) && a < C->base) {
			int t = bc_temp(C);
			bc_emit(C, BC_MOV, t, a, 0, NULL, NULL);
			a = t;
		}
		int b = bc_expr(C, r);
		bc_release(C, m);
		BinaryOp op = cond->as.binary.op;
		if (when)   /* (jump when a < b: unless a >= b; whole numbers, so exactly) */
			op = op == BIN_LT ? BIN_GE : op == BIN_LE ? BIN_GT : op == BIN_GT ? BIN_LE :
			     op == BIN_GE ? BIN_LT : op == BIN_EQ ? BIN_NE : BIN_EQ;
		int k = op == BIN_LT ? BC_JF_LT_I : op == BIN_LE ? BC_JF_LE_I : op == BIN_GT ? BC_JF_GT_I :
			op == BIN_GE ? BC_JF_GE_I : op == BIN_EQ ? BC_JF_EQ_I : BC_JF_NE_I;
		int at = bc_emit(C, k, 0, a, b, NULL, NULL);
		C->next = mark;
		return at;
	}
	int r = bc_expr(C, cond);
	bc_release(C, m);
	int at = bc_emit(C, when ? BC_JT : BC_JF, 0, r, 0, NULL, NULL);
	C->next = mark;
	return at;
}

static void bc_block(BcComp *C, Block *b);
static void bc_stmt(BcComp *C, Stmt *s);

/* an object (not a union) made with its fields given, every one given a
 * number: `{.Size = I % 100, .Wide = true}` */
static bool bc_object_init(const Expr *e)
{
	if (e->kind != EXPR_INIT || e->ty->kind != TY_OBJECT || e->ty->obj->kind == OBJ_UNION)
		return false;
	int given = 0;
	for (int j = 0; j < e->ty->obj->field_count; j++)
		if (e->as.init.slots[j]) {
			if (!bc_plain(e->as.init.slots[j]->ty) || !bc_plain(e->ty->obj->fields[j].type))
				return false;
			given++;
		}
	return given > 0;
}

/* a block's locals let go of, as the tree-walker does when it's left:
 * nothing to do for numbers, unless something has an address of one */
static void bc_drop(BcComp *C, Block *b)
{
	if (!b || !b->locals.len)
		return;
	bool things = false;
	for (int i = 0; i < b->locals.len; i++)
		things |= !bc_plain(b->locals.items[i]->type);
	bc_emit(C, BC_DROP, things, 0, 0, b, NULL);
}

/* the blocks open, from the innermost out to `depth`, left */
static void bc_leave_to(BcComp *C, int depth)
{
	for (int i = C->nblocks - 1; i >= depth; i--)
		bc_drop(C, C->blocks[i]);
}

static void bc_add(int **list, int *n, int at)
{
	*list = rt_realloc(*list, sizeof **list * (size_t)(*n + 1));
	(*list)[(*n)++] = at;
}

/* the loop a break (or a continue) goes to: a switch takes breaks only */
static BcLoop *bc_loop_for(BcComp *C, bool cont)
{
	for (int i = C->nloops - 1; i >= 0; i--)
		if (!cont || !C->loops[i]->is_switch)
			return C->loops[i];
	return NULL;
}

static void bc_break(BcComp *C, bool cont)
{
	BcLoop *L = bc_loop_for(C, cont);
	if (!L) {
		C->fail = true;
		return;
	}
	bc_leave_to(C, L->block_depth);
	int at = bc_emit(C, BC_JMP, 0, 0, 0, NULL, NULL);
	if (cont)
		bc_add(&L->conts, &L->nconts, at);
	else
		bc_add(&L->breaks, &L->nbreaks, at);
}

static void bc_return_now(BcComp *C)
{
	bc_leave_to(C, 0);
	bc_emit(C, BC_RET, 0, 0, 0, NULL, NULL);
}

/* the tree-walker runs s; what it returns with (return, break, continue)
 * goes on from here as it would have */
static void bc_fallback(BcComp *C, Stmt *s)
{
	int at = bc_emit(C, BC_STMT, -1, -1, -1, s, NULL);
	int over = bc_emit(C, BC_JMP, 0, 0, 0, NULL, NULL);
	C->bc->code[at].a = bc_here(C);
	bc_return_now(C);
	if (bc_loop_for(C, false)) {
		C->bc->code[at].b = bc_here(C);
		bc_break(C, false);
	}
	if (bc_loop_for(C, true)) {
		C->bc->code[at].c = bc_here(C);
		bc_break(C, true);
	}
	bc_patch(C, over, bc_here(C));
}

static void bc_loop_end(BcComp *C, BcLoop *L, int cont_to, int break_to)
{
	for (int i = 0; i < L->nconts; i++)
		bc_patch(C, L->conts[i], cont_to);
	for (int i = 0; i < L->nbreaks; i++)
		bc_patch(C, L->breaks[i], break_to);
	free(L->conts);
	free(L->breaks);
	C->nloops--;
}

static bool bc_push_loop(BcComp *C, BcLoop *L)
{
	if (C->nloops == 64) {
		C->fail = true;
		return false;
	}
	*L = (BcLoop){ C->nblocks, NULL, 0, NULL, 0, false };
	C->loops[C->nloops++] = L;
	return true;
}

/* an assignment to a function's own number variable, or to an element of
 * an array or list of numbers */
static bool bc_assign(BcComp *C, Expr *e)
{
	Expr *t = e->as.assign.target, *v = e->as.assign.value;
	if (bc_element(t) && bc_plain(v->ty)) {
		int mark = C->next;
		BcMark m = bc_mark(C);
		int r = bc_expr(C, v);   /* the value first, as the tree-walker does */
		if (!bc_calm(t->as.index.index) && r < C->base) {
			int k = bc_temp(C);
			bc_emit(C, BC_MOV, k, r, 0, NULL, NULL);
			r = k;
		}
		int i = bc_expr(C, t->as.index.index);
		bc_emit(C, e->as.assign.compound ? BC_INDEX_COMPOUND : BC_INDEX_SET, i, r, 0, t->as.index.base->as.ident.sym,
			e->as.assign.compound ? (void *)e : (void *)t);
		bc_release(C, m);
		C->next = mark;
		return true;
	}
	if (bc_lookup_element(t) && bc_plain(v->ty) && (t->as.index.lookup_put || e->as.assign.compound)) {
		int mark = C->next;
		BcMark m = bc_mark(C);
		Value lit;
		bool carried = bc_literal(v, &lit);
		int r = carried ? -1 : bc_expr(C, v);   /* the value first, as the tree-walker does */
		if (!carried && !bc_calm(t->as.index.index) && r < C->base) {
			int k = bc_temp(C);
			bc_emit(C, BC_MOV, k, r, 0, NULL, NULL);
			r = k;
		}
		Symbol *ks;
		int k = bc_key(C, t->as.index.index, &ks);
		int at = bc_emit_keyed(C, e->as.assign.compound ? BC_LK_COMPOUND : BC_LK_PUT, k, r, ks,
				       bc_variable(t->as.index.base), e);
		if (carried)
			C->bc->code[at].v = lit;
		Type *ot = e->ty, *tt = t->ty;
		if (e->as.assign.compound && tt->kind == TY_INT && type_eq(ot, tt) &&
		    (e->as.assign.op == BIN_ADD || e->as.assign.op == BIN_SUB))
			C->bc->code[at].k = 1;   /* (the quick way: see BC_LK_COMPOUND) */
		bc_release(C, m);
		C->next = mark;
		return true;
	}
	if (bc_field(t) && bc_plain(v->ty)) {
		int mark = C->next;
		BcMark m = bc_mark(C);
		int r = bc_expr(C, v);   /* the value first, as the tree-walker does */
		bc_emit(C, e->as.assign.compound ? BC_FIELD_COMPOUND : BC_FIELD_SET, 0, r, 0, bc_variable(t->as.field.object),
			e->as.assign.compound ? (void *)e : (void *)t);
		bc_release(C, m);
		C->next = mark;
		return true;
	}
	if (t->kind != EXPR_IDENT || !bc_plain_local(t->as.ident.sym) || !bc_plain(v->ty))
		return false;
	int dst = t->as.ident.sym->index;
	int mark = C->next;
	Value lit;
	if (e->as.assign.compound && bc_literal(v, &lit) && t->ty->kind == TY_INT && type_eq(e->ty, t->ty) &&
	    (e->as.assign.op == BIN_ADD || e->as.assign.op == BIN_SUB || e->as.assign.op == BIN_MUL)) {
		int at = bc_emit(C, BC_ADDK_TO_I, dst, 0, e->as.assign.op, (void *)t->ty, NULL);   /* `i += 1` */
		C->bc->code[at].k = bc_size(t->ty);
		C->bc->code[at].v = lit;
		return true;
	}
	BcMark m = bc_mark(C);
	int r = bc_expr(C, v);
	if (!e->as.assign.compound)
		bc_emit(C, BC_MOV, dst, r, 0, NULL, NULL);
	else if ((e->as.assign.op == BIN_ADD || e->as.assign.op == BIN_SUB || e->as.assign.op == BIN_MUL) &&
		 t->ty->kind == TY_INT && type_eq(e->ty, t->ty))
	{
		int at = bc_emit(C, BC_ADD_TO_I, dst, r, e->as.assign.op, (void *)t->ty, NULL);   /* (bc_emit can move the code) */
		C->bc->code[at].k = bc_size(t->ty);
	}
	else
		bc_emit(C, BC_COMPOUND, dst, r, 0, e, NULL);
	bc_release(C, m);
	C->next = mark;
	return true;
}

static void bc_stmt(BcComp *C, Stmt *s)
{
	if (C->fail)
		return;
	int mark = C->next;
	switch (s->kind) {
	case STMT_DECL: {
		/* (a variable of any kind: what isn't a number is worked out by the
		 * tree-walker, straight into its cell, as exec_stmt does) */
		bool native = true;
		for (int i = 0; i < s->as.decl.len; i++) {
			VarDecl *v = s->as.decl.items[i];
			if (!v->sym || v->sym->kind == SYM_GLOBAL)
				native = false;
		}
		if (!native) {
			bc_fallback(C, s);
			break;
		}
		BcMark m = bc_mark(C);
		for (int i = 0; i < s->as.decl.len; i++) {
			VarDecl *v = s->as.decl.items[i];
			Expr *in = v->init;
			if (in && in->kind == EXPR_CALL && in->as.call.builtin == BUILTIN_STR_SLICE &&
			    bc_variable(in->as.call.args.items[0]) && bc_plain(in->as.call.args.items[1]->ty) &&
			    bc_plain(in->as.call.args.items[2]->ty)) {
				/* `str Name = Line[to Colon - 1]`: its ends worked out here */
				int mk = C->next;
				int from = bc_expr(C, in->as.call.args.items[1]);
				if (!bc_calm(in->as.call.args.items[2]) && from < C->base) {
					int t = bc_temp(C);
					bc_emit(C, BC_MOV, t, from, 0, NULL, NULL);
					from = t;
				}
				int to = bc_expr(C, in->as.call.args.items[2]);
				int at = bc_emit(C, BC_SLICE, v->sym->index, from, to, NULL, in);
				C->bc->code[at].r = bc_variable(in->as.call.args.items[0]);
				C->next = mk;
			} else if (in && bc_object_init(in)) {
				/* `square S = {.Size = I % 100}`: the numbers worked out here */
				int mk = C->next, first = -1, given = 0;
				Expr **slots = in->as.init.slots;
				for (int j = 0; j < in->ty->obj->field_count; j++)
					if (slots[j]) {   /* side by side, before any is worked out */
						int t = bc_temp(C);
						if (first < 0)
							first = t;
					}
				for (int j = 0; j < in->ty->obj->field_count; j++)
					if (slots[j]) {
						int part = C->next;
						bc_emit(C, BC_MOV, first + given++, bc_expr(C, slots[j]), 0, NULL, NULL);
						C->next = part;
					}
				bc_emit(C, BC_INIT, v->sym->index, first, 0, NULL, in);
				C->next = mk;
			} else if (v->init && !bc_plain(v->sym->type)) {
				bc_emit(C, bc_str_builtin(v->init) ? BC_STRB : BC_EVAL, v->sym->index, 0, 0, v->init, NULL);
				C->evals++;
			} else if (!v->init && !bc_plain(v->sym->type)) {
				bc_emit(C, BC_ZERO, v->sym->index, 0, 0, v->sym->type, NULL);
			} else if (v->init) {
				int mk = C->next;
				int r = bc_expr(C, v->init);
				bc_emit(C, BC_MOV, v->sym->index, r, 0, NULL, NULL);
				C->next = mk;
			} else {
				int at = bc_emit(C, BC_CONST, v->sym->index, 0, 0, NULL, NULL);
				C->bc->code[at].v = zero_value(v->sym->type);
			}
		}
		bc_release(C, m);
		break;
	}
	case STMT_EXPR: {
		Expr *e = s->as.expr;
		if (e->kind == EXPR_ASSIGN && bc_assign(C, e))
			break;
		if (e->kind == EXPR_CALL && e->as.call.builtin == BUILTIN_LIST_ADD && e->as.call.args.len == 2 &&
		    bc_variable(e->as.call.args.items[0]) && bc_plain(e->as.call.args.items[1]->ty)) {
			const Type *lt = e->as.call.args.items[0]->ty;
			while (lt && lt->kind == TY_REF)
				lt = lt->inner;
			if (lt && lt->kind == TY_LIST && bc_plain(lt->inner)) {
				BcMark m = bc_mark(C);
				int r = bc_expr(C, e->as.call.args.items[1]);
				bc_emit(C, BC_LIST_ADD, r, 0, 0, bc_variable(e->as.call.args.items[0]), e);
				bc_release(C, m);
				break;
			}
		}
		if (e->kind == EXPR_CALL && e->as.call.builtin == BUILTIN_LOOKUP_REMOVE && e->as.call.args.len == 2 &&
		    bc_variable(e->as.call.args.items[0])) {
			Expr *key = e->as.call.args.items[1];
			bool by_number = bc_plain(key->ty), by_variable = !by_number && bc_variable(key);
			if (by_number || by_variable) {
				BcMark m = bc_mark(C);
				Symbol *ks;
				int k = bc_key(C, key, &ks);
				bc_emit_keyed(C, BC_LK_REMOVE, 0, k, ks, bc_variable(e->as.call.args.items[0]), e);
				bc_release(C, m);
				break;
			}
		}
		if (e->kind == EXPR_CALL && bc_direct_call(e)) {
			BcMark m = bc_mark(C);
			int r = bc_call(C, e);
			Type *rt = e->as.call.fn->definition ? e->as.call.fn->definition->ret_type : e->as.call.fn->ret_type;
			if (rt->kind != TY_VOID && !bc_plain(rt))
				bc_emit(C, BC_DROPVAL, r, 0, 0, NULL, NULL);   /* (an answer nobody wants) */
			bc_release(C, m);
			break;
		}
		bc_fallback(C, s);
		break;
	}
	case STMT_IF: {
		int jf = bc_jump_unless(C, s->as.if_.cond);
		bc_block(C, s->as.if_.then_block);
		if (s->as.if_.else_branch) {
			int over = bc_emit(C, BC_JMP, 0, 0, 0, NULL, NULL);
			bc_patch(C, jf, bc_here(C));
			bc_stmt(C, s->as.if_.else_branch);
			bc_patch(C, over, bc_here(C));
		} else {
			bc_patch(C, jf, bc_here(C));
		}
		break;
	}
	case STMT_BLOCK:
		bc_block(C, s->as.block);
		break;
	case STMT_WHILE: {
		BcLoop L;
		if (!bc_push_loop(C, &L))
			break;
		/* the test, then round: tick, body, the test again at the bottom */
		int jf = bc_jump_unless(C, s->as.while_.cond);
		int top = bc_here(C);
		bc_emit(C, BC_TICK, 0, 0, 0, NULL, NULL);
		bc_block(C, s->as.while_.body);
		int again = bc_here(C);
		bc_patch(C, bc_jump_if(C, s->as.while_.cond), top);
		bc_patch(C, jf, bc_here(C));
		bc_loop_end(C, &L, again, bc_here(C));
		break;
	}
	case STMT_DO: {
		BcLoop L;
		if (!bc_push_loop(C, &L))
			break;
		int top = bc_here(C);
		bc_emit(C, BC_TICK, 0, 0, 0, NULL, NULL);
		bc_block(C, s->as.while_.body);
		int cond = bc_here(C);
		int jf = bc_jump_unless(C, s->as.while_.cond);
		bc_emit(C, BC_JMP, top, 0, 0, NULL, NULL);
		bc_patch(C, jf, bc_here(C));
		bc_loop_end(C, &L, cond, bc_here(C));
		break;
	}
	case STMT_FOR: {
		if (C->nblocks == 256) {
			C->fail = true;
			break;
		}
		C->blocks[C->nblocks++] = s->as.for_.scope;
		if (s->as.for_.init)
			bc_stmt(C, s->as.for_.init);
		BcLoop L;
		if (!bc_push_loop(C, &L))
			break;
		/* the test, then round: tick, body, update, the test again */
		int jf = s->as.for_.cond ? bc_jump_unless(C, s->as.for_.cond) : -1;
		int top = bc_here(C);
		bc_emit(C, BC_TICK, 0, 0, 0, NULL, NULL);
		bc_block(C, s->as.for_.body);
		int update = bc_here(C);
		if (s->as.for_.update)
			bc_stmt(C, s->as.for_.update);
		if (s->as.for_.cond)
			bc_patch(C, bc_jump_if(C, s->as.for_.cond), top);
		else
			bc_emit(C, BC_JMP, top, 0, 0, NULL, NULL);
		int end = bc_here(C);
		if (jf >= 0)
			bc_patch(C, jf, end);
		bc_loop_end(C, &L, update, end);
		C->nblocks--;
		bc_drop(C, s->as.for_.scope);
		break;
	}
	case STMT_RETURN: {
		Expr *v = s->as.ret.value;
		if (v && !bc_plain(v->ty)) {
			bc_fallback(C, s);
			break;
		}
		if (v) {
			BcMark m = bc_mark(C);
			int r = bc_expr(C, v);
			bc_emit(C, BC_SETRET, r, 0, 0, NULL, NULL);
			bc_release(C, m);
		}
		bc_return_now(C);
		break;
	}
	case STMT_SWITCH: {
		/* the cases' values tested in order, as the tree-walker does, then
		 * the bodies one after another (falling through, to a break) */
		CaseVec *cases = &s->as.switch_.cases;
		BcMark m = bc_mark(C);
		int r = bc_expr(C, s->as.switch_.value);
		bc_release(C, m);
		int **tests = rt_alloc_zeroed(sizeof *tests * (size_t)(cases->len + 1));
		for (int i = 0; i < cases->len; i++) {
			tests[i] = rt_alloc_zeroed(sizeof **tests * (size_t)(cases->items[i].values.len + 1));
			for (int j = 0; j < cases->items[i].values.len; j++) {
				tests[i][j] = bc_emit(C, BC_JEQ_K, 0, r, 0, NULL, NULL);
				C->bc->code[tests[i][j]].v = v_int(cases->items[i].consts[j]);
			}
		}
		int none = bc_emit(C, BC_JMP, 0, 0, 0, NULL, NULL);
		BcLoop L;
		if (!bc_push_loop(C, &L)) {
			for (int i = 0; i < cases->len; i++)
				rt_free(tests[i]);
			rt_free(tests);
			break;
		}
		L.is_switch = true;
		for (int i = 0; i < cases->len; i++) {
			for (int j = 0; j < cases->items[i].values.len; j++)
				bc_patch(C, tests[i][j], bc_here(C));
			if (i == s->as.switch_.default_index)
				bc_patch(C, none, bc_here(C));
			bc_block(C, cases->items[i].body);
			rt_free(tests[i]);
		}
		rt_free(tests);
		if (s->as.switch_.default_index < 0)
			bc_patch(C, none, bc_here(C));
		bc_loop_end(C, &L, bc_here(C), bc_here(C));
		break;
	}
	case STMT_BREAK:
		bc_break(C, false);
		break;
	case STMT_CONTINUE:
		bc_break(C, true);
		break;
	default:
		bc_fallback(C, s);
		break;
	}
	C->next = mark;
}

static void bc_block(BcComp *C, Block *b)
{
	if (C->nblocks == 256) {
		C->fail = true;
		return;
	}
	C->blocks[C->nblocks++] = b;
	for (int i = 0; i < b->stmts.len && !C->fail; i++)
		bc_stmt(C, b->stmts.items[i]);
	C->nblocks--;
	bc_drop(C, b);
}

/* the NOPs taken out, and the jumps moved to match */
static bool bc_jumps(int op)
{
	return op == BC_JMP || op == BC_JF || op == BC_JT || (op >= BC_JF_LT_I && op <= BC_JF_NE_I) || op == BC_JEQ_K;
}

static void bc_squeeze(Bytecode *bc)
{
	int *to = rt_alloc(sizeof *to * (size_t)(bc->n + 1));
	int n = 0;
	for (int i = 0; i < bc->n; i++) {
		to[i] = n;
		if (bc->code[i].op != BC_NOP)
			n++;
	}
	to[bc->n] = n;
	int k = 0;
	for (int i = 0; i < bc->n; i++) {
		BcIns in = bc->code[i];
		if (in.op == BC_NOP)
			continue;
		if (bc_jumps(in.op))
			in.a = to[in.a];
		if (in.op == BC_STMT) {
			in.a = in.a >= 0 ? to[in.a] : -1;
			in.b = in.b >= 0 ? to[in.b] : -1;
			in.c = in.c >= 0 ? to[in.c] : -1;
		}
		bc->code[k++] = in;
	}
	bc->n = k;
	rt_free(to);
}

/* fn's bytecode, made the first time it's asked for (NULL: the tree-walker
 * runs it) */
static Bytecode *bc_of(FnDecl *fn)
{
	if (fn->bc_state == 1)
		return fn->bytecode;
	if (fn->bc_state == 2 || g_bc_off)
		return NULL;
	fn->bc_state = 2;
	if (!fn->body || bc_refuse_block(fn->body))
		return NULL;
	Bytecode *bc = rt_alloc_zeroed(sizeof *bc);
	BcComp C = { .bc = bc, .fn = fn, .base = fn->frame.len };
	bc_block(&C, fn->body);
	bc_emit(&C, BC_RET, 0, 0, 0, NULL, NULL);
	if (C.fail) {
		rt_free(bc->code);
		rt_free(bc);
		return NULL;
	}
	bc->quick = !fn->self_sym && !is_header_file(fn->loc.file);
	for (int i = 0; i < fn->frame.len && bc->quick; i++)
		bc->quick = bc_plain_local(fn->frame.items[i]);
	/* (only numbers: nothing to let go of, and its cells aren't zeroed, so
	 * nothing in them is let go of either) */
	for (int i = 0; i < bc->n && bc->quick; i++)
		if (bc->code[i].op == BC_DROP)
			bc->code[i].op = BC_NOP;
	bc_squeeze(bc);
	bc->ntemps = C.max;
	fn->bytecode = bc;
	fn->bc_state = 1;
	bc->next_made = g_bc_made;
	g_bc_made = bc;
	return bc;
}

/* every function's bytecode let go of, at the program's end */
static void bc_free_all(void)
{
	while (g_bc_made) {
		Bytecode *next = g_bc_made->next_made;
		rt_free(g_bc_made->code);
		rt_free(g_bc_made);
		g_bc_made = next;
	}
}

/* ------------------------------------------------------------ running it */

static void bc_run(Interp *I, Bytecode *bc);

/* a call of a function whose variables are all numbers: call_function's
 * work, less what only other functions need (a library's call, letting go
 * of variables, the addresses of them) */
static Value bc_quick_call(Interp *I, FnDecl *fn, Bytecode *bc, const Value *args, SrcLoc loc)
{
	check_interrupt();
	if (++I->depth > (I->max_depth ? I->max_depth : MAX_DEPTH)) {
		set_panic_loc(loc);
		rt_panic(file_of(loc), loc.line, loc.col, "stack overflow: more than %d nested calls (calling '%s')",
			 I->max_depth ? I->max_depth : MAX_DEPTH, fn->name);
	}
	int room = fn->frame.len + bc->ntemps;
	if (!room)
		room = 1;
	/* room on the call stack, not zeroed: every variable is a number, and
	 * set before it's read */
	CellChunk *ch = I->chunk;
	Value *cells;
	if (ch && ch->used + room <= ch->cap) {
		cells = ch->cells + ch->used;
		ch->used += room;
	} else {
		cells = cells_push(I, room);
	}
	Frame f = { cells, fn->frame.len, v_void(), I->frame, fn };
	for (int i = 0; i < fn->params.len; i++)
		f.cells[fn->params.items[i]->sym->index] = args[i];
	Frame *saved = I->frame;
	I->frame = &f;
	bc_run(I, bc);
	I->frame = saved;
	cells_pop(I, room);
	I->depth--;
	return f.ret;
}

/* a lookup element's key: in a working, or (c 1) the variable r */
#define BC_KEY(reg) (in->c ? deref_all(symbol_cell(I, in->r)) : &R[reg])

/* the field e (an EXPR_FIELD) of the object in s */
static Value *bc_field_at(Interp *I, Symbol *s, const Expr *e)
{
	Object *o = deref_all(symbol_cell(I, s))->as.obj;
	return &o->fields[o->cls->block_start[e->as.field.owner_id] + e->as.field.field_index];
}

/* element i of the array or list in s: checked as the tree-walker checks.
 * NULL for an array of numbers as bytes: *raw is then the array. */
static Value *bc_element_at(Interp *I, Symbol *s, int64_t i, const Expr *e, Array **raw)
{
	Value *base = deref_all(symbol_cell(I, s));
	if (base->kind == V_LIST) {
		List *l = base->as.list;
		list_index_check(i, l->len, false, e->loc);
		return &l->items[i];
	}
	Array *a = base->as.arr;
	if ((uint64_t)i >= (uint64_t)a->len) {
		set_panic_loc(e->loc);
		rt_bounds_check(i, a->len, file_of(e->loc), e->loc.line, e->loc.col);
	}
	if (a->raw) {
		*raw = a;
		return NULL;
	}
	return &a->elems[i];
}

/* Each instruction goes straight on to the next: through a table of where
 * each one's code is (computed goto, which gcc and clang have), or, with
 * another compiler, a switch. */
#if (defined(__GNUC__) || defined(__clang__)) && !defined(BC_SWITCH)
#define BC_THREADED 1
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"   /* (&&label: gcc's and clang's own) */
#define CASE(op) L_##op
#define NEXT do { in = &code[pc++]; goto *labels[in->op]; } while (0)
#else
#define CASE(op) case op
#define NEXT break
#endif

static void bc_run(Interp *I, Bytecode *bc)
{
	Value *R = I->frame->cells;
	const BcIns *code = bc->code;
	int pc = 0;
	const BcIns *in;
#ifdef BC_THREADED
	static void *const labels[] = {   /* (each by its name: the order can't slip) */
		[BC_CONST] = &&L_BC_CONST,
		[BC_MOV] = &&L_BC_MOV,
		[BC_ADD_I] = &&L_BC_ADD_I,
		[BC_SUB_I] = &&L_BC_SUB_I,
		[BC_MUL_I] = &&L_BC_MUL_I,
		[BC_ADD_F] = &&L_BC_ADD_F,
		[BC_SUB_F] = &&L_BC_SUB_F,
		[BC_MUL_F] = &&L_BC_MUL_F,
		[BC_DIV_F] = &&L_BC_DIV_F,
		[BC_ARITH] = &&L_BC_ARITH,
		[BC_DIVMOD_I] = &&L_BC_DIVMOD_I,
		[BC_FMOD] = &&L_BC_FMOD,
		[BC_LT_I] = &&L_BC_LT_I,
		[BC_LE_I] = &&L_BC_LE_I,
		[BC_GT_I] = &&L_BC_GT_I,
		[BC_GE_I] = &&L_BC_GE_I,
		[BC_EQ_I] = &&L_BC_EQ_I,
		[BC_NE_I] = &&L_BC_NE_I,
		[BC_CMP] = &&L_BC_CMP,
		[BC_NEG_I] = &&L_BC_NEG_I,
		[BC_NEG_F] = &&L_BC_NEG_F,
		[BC_NOT] = &&L_BC_NOT,
		[BC_BITNOT] = &&L_BC_BITNOT,
		[BC_CONVERT] = &&L_BC_CONVERT,
		[BC_WRAP_I] = &&L_BC_WRAP_I,
		[BC_JMP] = &&L_BC_JMP,
		[BC_JF] = &&L_BC_JF,
		[BC_JT] = &&L_BC_JT,
		[BC_JF_LT_I] = &&L_BC_JF_LT_I,
		[BC_JF_LE_I] = &&L_BC_JF_LE_I,
		[BC_JF_GT_I] = &&L_BC_JF_GT_I,
		[BC_JF_GE_I] = &&L_BC_JF_GE_I,
		[BC_JF_EQ_I] = &&L_BC_JF_EQ_I,
		[BC_JF_NE_I] = &&L_BC_JF_NE_I,
		[BC_JEQ_K] = &&L_BC_JEQ_K,
		[BC_INIT] = &&L_BC_INIT,
		[BC_CALL] = &&L_BC_CALL,
		[BC_EVAL] = &&L_BC_EVAL,
		[BC_STMT] = &&L_BC_STMT,
		[BC_COMPOUND] = &&L_BC_COMPOUND,
		[BC_ADD_TO_I] = &&L_BC_ADD_TO_I,
		[BC_ADDK_TO_I] = &&L_BC_ADDK_TO_I,
		[BC_DROP] = &&L_BC_DROP,
		[BC_TICK] = &&L_BC_TICK,
		[BC_SETRET] = &&L_BC_SETRET,
		[BC_RET] = &&L_BC_RET,
		[BC_INDEX_GET] = &&L_BC_INDEX_GET,
		[BC_INDEX_SET] = &&L_BC_INDEX_SET,
		[BC_INDEX_COMPOUND] = &&L_BC_INDEX_COMPOUND,
		[BC_CALLM] = &&L_BC_CALLM,
		[BC_DROPVAL] = &&L_BC_DROPVAL,
		[BC_FIELD_GET] = &&L_BC_FIELD_GET,
		[BC_FIELD_SET] = &&L_BC_FIELD_SET,
		[BC_FIELD_COMPOUND] = &&L_BC_FIELD_COMPOUND,
		[BC_LEN] = &&L_BC_LEN,
		[BC_STR_GET] = &&L_BC_STR_GET,
		[BC_LK_HAS] = &&L_BC_LK_HAS,
		[BC_LK_GET] = &&L_BC_LK_GET,
		[BC_LK_PUT] = &&L_BC_LK_PUT,
		[BC_LK_COMPOUND] = &&L_BC_LK_COMPOUND,
		[BC_LK_PART] = &&L_BC_LK_PART,
		[BC_LK_NEXT] = &&L_BC_LK_NEXT,
		[BC_ZERO] = &&L_BC_ZERO,
		[BC_LIST_ADD] = &&L_BC_LIST_ADD,
		[BC_SLICE] = &&L_BC_SLICE,
		[BC_LK_REMOVE] = &&L_BC_LK_REMOVE,
		[BC_STRB] = &&L_BC_STRB,
		[BC_MARK] = &&L_BC_MARK,
		[BC_RELEASE] = &&L_BC_RELEASE,
		[BC_NOP] = &&L_BC_NOP,
	};
	static bool checked;
	if (!checked) {
		checked = true;
		for (int i = 0; i < BC_COUNT; i++)
			if (!labels[i])
				internal_error(((FnDecl *)I->frame->fn)->loc, "the bytecode's table has no place for an instruction");
	}
	NEXT;
	{
#else
	for (;;) {
		in = &code[pc++];
		switch (in->op) {
#endif
		CASE(BC_CONST): R[in->a] = in->v; NEXT;
		CASE(BC_MOV):   R[in->a] = R[in->b]; NEXT;
		CASE(BC_ADD_I):
			R[in->a] = v_int(bc_wrap((int64_t)((uint64_t)R[in->b].as.i + (uint64_t)R[in->c].as.i), in->k));
			NEXT;
		CASE(BC_SUB_I):
			R[in->a] = v_int(bc_wrap((int64_t)((uint64_t)R[in->b].as.i - (uint64_t)R[in->c].as.i), in->k));
			NEXT;
		CASE(BC_MUL_I):
			R[in->a] = v_int(bc_wrap((int64_t)((uint64_t)R[in->b].as.i * (uint64_t)R[in->c].as.i), in->k));
			NEXT;
		CASE(BC_ADD_F): R[in->a] = v_float(R[in->b].as.f + R[in->c].as.f); NEXT;
		CASE(BC_SUB_F): R[in->a] = v_float(R[in->b].as.f - R[in->c].as.f); NEXT;
		CASE(BC_MUL_F): R[in->a] = v_float(R[in->b].as.f * R[in->c].as.f); NEXT;
		CASE(BC_DIV_F): R[in->a] = v_float(R[in->b].as.f / R[in->c].as.f); NEXT;
		CASE(BC_ARITH): {
			Expr *e = in->p;
			R[in->a] = arith(e->as.binary.op, R[in->b], R[in->c], e->ty, e->loc);
			NEXT;
		}
		CASE(BC_DIVMOD_I): {
			Expr *e = in->p;
			int64_t x = R[in->b].as.i, y = R[in->c].as.i;
			if (y == 0 || y == -1) {   /* the checks, and their panics: arith's */
				R[in->a] = arith(e->as.binary.op, R[in->b], R[in->c], e->ty, e->loc);
				NEXT;
			}
			bool div = e->as.binary.op == BIN_DIV;
			int64_t z;
			if (in->k < 0 || (in->k == 64 && !e->ty->is_signed))
				z = (int64_t)(div ? (uint64_t)x / (uint64_t)y : (uint64_t)x % (uint64_t)y);
			else
				z = div ? x / y : x % y;
			R[in->a] = v_int(bc_wrap(z, in->k));
			NEXT;
		}
		CASE(BC_FMOD): R[in->a] = v_float(fmod(R[in->b].as.f, R[in->c].as.f)); NEXT;
		CASE(BC_LT_I): R[in->a] = v_bool(R[in->b].as.i < R[in->c].as.i); NEXT;
		CASE(BC_LE_I): R[in->a] = v_bool(R[in->b].as.i <= R[in->c].as.i); NEXT;
		CASE(BC_GT_I): R[in->a] = v_bool(R[in->b].as.i > R[in->c].as.i); NEXT;
		CASE(BC_GE_I): R[in->a] = v_bool(R[in->b].as.i >= R[in->c].as.i); NEXT;
		CASE(BC_EQ_I): R[in->a] = v_bool(R[in->b].as.i == R[in->c].as.i); NEXT;
		CASE(BC_NE_I): R[in->a] = v_bool(R[in->b].as.i != R[in->c].as.i); NEXT;
		CASE(BC_CMP): {
			Expr *e = in->p;
			const Type *t = e->as.binary.lhs->ty;
			const Value *x = &R[in->b], *y = &R[in->c];
			bool r;
			if (t->kind == TY_FLOAT && (isnan(x->as.f) || isnan(y->as.f))) {
				r = e->as.binary.op == BIN_NE;
			} else {
				int c = compare(x, y, t);
				switch (e->as.binary.op) {
				case BIN_EQ: r = c == 0; break;
				case BIN_NE: r = c != 0; break;
				case BIN_LT: r = c < 0; break;
				case BIN_LE: r = c <= 0; break;
				case BIN_GT: r = c > 0; break;
				default:     r = c >= 0; break;
				}
			}
			R[in->a] = v_bool(r);
			NEXT;
		}
		CASE(BC_NEG_I): R[in->a] = v_int(norm((int64_t)(0 - (uint64_t)R[in->b].as.i), in->p)); NEXT;
		CASE(BC_NEG_F): R[in->a] = v_float(-R[in->b].as.f); NEXT;
		CASE(BC_NOT):   R[in->a] = v_bool(!R[in->b].as.b); NEXT;
		CASE(BC_BITNOT): R[in->a] = v_int(norm(~R[in->b].as.i, in->p)); NEXT;
		CASE(BC_CONVERT): R[in->a] = convert(R[in->b], in->p, in->q); NEXT;
		CASE(BC_WRAP_I): R[in->a] = v_int(bc_wrap(R[in->b].as.i, in->k)); NEXT;
		CASE(BC_JMP):   pc = in->a; NEXT;
		CASE(BC_JF):    if (!R[in->b].as.b) pc = in->a; NEXT;
		CASE(BC_JT):    if (R[in->b].as.b) pc = in->a; NEXT;
		CASE(BC_JF_LT_I): if (!(R[in->b].as.i < R[in->c].as.i)) pc = in->a; NEXT;
		CASE(BC_JF_LE_I): if (!(R[in->b].as.i <= R[in->c].as.i)) pc = in->a; NEXT;
		CASE(BC_JF_GT_I): if (!(R[in->b].as.i > R[in->c].as.i)) pc = in->a; NEXT;
		CASE(BC_JF_GE_I): if (!(R[in->b].as.i >= R[in->c].as.i)) pc = in->a; NEXT;
		CASE(BC_JF_EQ_I): if (!(R[in->b].as.i == R[in->c].as.i)) pc = in->a; NEXT;
		CASE(BC_JF_NE_I): if (!(R[in->b].as.i != R[in->c].as.i)) pc = in->a; NEXT;
		CASE(BC_JEQ_K): if (R[in->b].as.i == in->v.as.i) pc = in->a; NEXT;
		CASE(BC_INIT): {   /* as eval_init makes an object */
			Expr *e = in->q;
			ObjectInfo *o = e->ty->obj;
			Object *ob = rt_alloc(sizeof *ob + sizeof(Value) * (size_t)o->field_count);
			ob->cls = o;
			int from = in->b;
			for (int j = 0; j < o->field_count; j++)
				ob->fields[j] = e->as.init.slots[j] ? R[from++] : zero_value(o->fields[j].type);
			run_makers(ob);
			R[in->a] = (Value){ .kind = V_OBJECT, .as.obj = ob };
			NEXT;
		}
		CASE(BC_CALL): {
			Expr *e = in->q;
			FnDecl *fn = in->p;
			Bytecode *cb = in->r;
			if (!cb && fn->bc_state != 2)
				cb = ((BcIns *)in)->r = bc_of(fn);   /* (asked once: kept with the call) */
			if (cb && cb->quick && !g_nlive_watches)
				R[in->a] = bc_quick_call(I, fn, cb, &R[in->b], e->loc);
			else
				R[in->a] = call_function(I, fn, NULL, &R[in->b], e->loc);
			NEXT;
		}
		CASE(BC_EVAL):
			R[in->a] = eval(I, in->p);
			NEXT;
		CASE(BC_INDEX_GET): {
			Array *raw;
			Value *place = bc_element_at(I, in->p, R[in->b].as.i, in->q, &raw);
			R[in->a] = place ? *place : raw_get(raw, R[in->b].as.i);
			NEXT;
		}
		CASE(BC_INDEX_SET): {
			Array *raw;
			Value *place = bc_element_at(I, in->p, R[in->a].as.i, in->q, &raw);
			if (place)
				*place = R[in->b];
			else
				raw_put(raw, R[in->a].as.i, &R[in->b]);
			NEXT;
		}
		CASE(BC_INDEX_COMPOUND): {
			Expr *e = in->q;
			Array *raw;
			Value *place = bc_element_at(I, in->p, R[in->a].as.i, e->as.assign.target, &raw);
			Type *ot = e->ty, *tt = e->as.assign.target->ty;
			Value was = place ? *place : raw_get(raw, R[in->a].as.i);
			Value old = convert(was, tt, ot), res;
			if (e->as.assign.op == BIN_MOD && ot->kind == TY_FLOAT)
				res = v_float(fmod(old.as.f, R[in->b].as.f));
			else
				res = arith(e->as.assign.op, old, R[in->b], ot, e->loc);
			res = convert(res, ot, tt);
			if (place)
				*place = res;
			else
				raw_put(raw, R[in->a].as.i, &res);
			NEXT;
		}
		CASE(BC_CALLM): {
			Expr *e = in->q;
			FnDecl *fn = in->p;
			Value self = v_ref(symbol_cell(I, in->r));
			while (self.as.ref->kind == V_REF)
				self = *self.as.ref;
			if (e->as.call.slot >= 0)
				fn = self.as.ref->as.obj->cls->vtable[e->as.call.slot];   /* dynamic dispatch */
			R[in->a] = call_function(I, fn, &self, &R[in->b], e->loc);
			NEXT;
		}
		CASE(BC_DROPVAL):
			value_drop(I, &R[in->a]);
			NEXT;
		CASE(BC_FIELD_GET):
			R[in->a] = *bc_field_at(I, in->p, in->q);
			NEXT;
		CASE(BC_FIELD_SET):
			*bc_field_at(I, in->p, in->q) = R[in->b];
			NEXT;
		CASE(BC_FIELD_COMPOUND): {
			Expr *e = in->q;
			Value *place = bc_field_at(I, in->p, e->as.assign.target);
			Type *ot = e->ty, *tt = e->as.assign.target->ty;
			Value old = convert(*place, tt, ot), res;
			if (e->as.assign.op == BIN_MOD && ot->kind == TY_FLOAT)
				res = v_float(fmod(old.as.f, R[in->b].as.f));
			else
				res = arith(e->as.assign.op, old, R[in->b], ot, e->loc);
			*place = convert(res, ot, tt);
			NEXT;
		}
		CASE(BC_LEN): {
			Value *v = symbol_cell(I, in->p);
			R[in->a] = v_int(in->k == 0 ? str_of(v)->len : in->k == 1 ? list_of(v)->len :
					 in->k == 2 ? deref_all(v)->as.arr->len : lookup_of(v)->len);
			NEXT;
		}
		CASE(BC_STR_GET): {
			Str *t = str_of(symbol_cell(I, in->p));
			int64_t i = str_index(t, R[in->b].as.i, ((Expr *)in->q)->loc);
			R[in->a] = v_int((int8_t)t->data[i]);
			NEXT;
		}
		CASE(BC_LK_HAS):
			R[in->a] = v_bool(lk_find(lookup_of(symbol_cell(I, in->p)), BC_KEY(in->b)) >= 0);
			NEXT;
		CASE(BC_LK_GET): {
			Expr *e = in->q;
			Lookup *k = lookup_of(symbol_cell(I, in->p));
			int64_t pos = lk_find(k, BC_KEY(in->b));
			if (pos < 0)
				lk_missing(BC_KEY(in->b), e->as.index.base->ty->key, e->loc);
			R[in->a] = k->vals[pos];
			NEXT;
		}
		CASE(BC_LK_PUT): {
			Lookup *k = lookup_of(symbol_cell(I, in->p));
			bool is_new;
			int64_t pos = lk_put(k, BC_KEY(in->a), &is_new);
			k->vals[pos] = in->b < 0 ? in->v : R[in->b];   /* (an old number: nothing to let go of) */
			NEXT;
		}
		CASE(BC_LK_COMPOUND): {
			Expr *e = in->q, *t = e->as.assign.target;
			Lookup *k = lookup_of(symbol_cell(I, in->p));
			int64_t pos = lk_find(k, BC_KEY(in->a));
			if (pos < 0)
				lk_missing(BC_KEY(in->a), t->as.index.base->ty->key, t->loc);
			Value *place = &k->vals[pos];
			const Value *by = in->b < 0 ? &in->v : &R[in->b];
			if (in->k) {   /* (whole numbers of one type, + or -: wrapped, as a variable's are) */
				uint64_t x = (uint64_t)place->as.i, y = (uint64_t)by->as.i;
				*place = v_int(bc_wrap((int64_t)(e->as.assign.op == BIN_ADD ? x + y : x - y), bc_size(t->ty)));
				NEXT;
			}
			Type *ot = e->ty, *tt = t->ty;
			Value old = convert(*place, tt, ot), res;
			if (e->as.assign.op == BIN_MOD && ot->kind == TY_FLOAT)
				res = v_float(fmod(old.as.f, by->as.f));
			else
				res = arith(e->as.assign.op, old, *by, ot, e->loc);
			*place = convert(res, ot, tt);
			NEXT;
		}
		CASE(BC_LK_PART): {
			Lookup *k = lookup_of(symbol_cell(I, in->p));
			int64_t pos = R[in->b].as.i;
			R[in->a] = in->c == 1 ? k->keys[pos] : k->vals[pos];
			NEXT;
		}
		CASE(BC_LK_NEXT): {
			Lookup *k = lookup_of(symbol_cell(I, in->p));
			int64_t pos = R[in->b].as.i;
			while (pos < k->n && k->dead[pos])
				pos++;
			R[in->a] = v_int(pos < k->n ? pos : -1);
			NEXT;
		}
		CASE(BC_STRB):
			R[in->a] = eval_str_builtin(I, in->p);
			NEXT;
		CASE(BC_LIST_ADD): {
			List *l = list_of(symbol_cell(I, in->p));
			list_reserve(l, 1);
			l->items[l->len++] = R[in->a];
			list_resized(l, "an item was added");
			NEXT;
		}
		CASE(BC_LK_REMOVE): {
			Expr *e = in->q;
			Lookup *k = lookup_of(symbol_cell(I, in->p));
			Value *key = BC_KEY(in->b);
			int64_t pos = lk_find(k, key);
			if (pos < 0)
				lk_missing(key, e->as.call.args.items[0]->ty->key, e->loc);
			lk_kill(I, k, pos);
			NEXT;
		}
		CASE(BC_SLICE): {
			/* (as the tree-walker's BUILTIN_STR_SLICE) */
			Expr *e = in->q;
			Str *t = str_of(symbol_cell(I, in->r));
			int flags = (int)e->as.call.args.items[3]->as.int_lit.value;
			int64_t a, n;
			set_panic_loc(e->loc);
			rt_str_slice(t->len, R[in->b].as.i, R[in->c].as.i, flags, &a, &n, file_of(e->loc), e->loc.line, e->loc.col);
			R[in->a] = (Value){ .kind = V_STR, .as.s = str_new(t->data + a, n) };
			NEXT;
		}
		CASE(BC_ZERO):
			R[in->a] = zero_value(in->p);
			NEXT;
		CASE(BC_MARK):
			R[in->a] = v_int(I->ntemps);
			NEXT;
		CASE(BC_RELEASE):
			release_temps(I, (int)R[in->a].as.i);
			NEXT;
		CASE(BC_NOP):
			NEXT;
		CASE(BC_STMT): {
			int mark = I->ntemps;
			Exec ex = exec_stmt(I, in->p);
			release_temps(I, mark);
			if (ex == EX_RETURN)
				pc = in->a;
			else if (ex == EX_BREAK && in->b >= 0)
				pc = in->b;
			else if (ex == EX_CONTINUE && in->c >= 0)
				pc = in->c;
			NEXT;
		}
		CASE(BC_COMPOUND): {
			Expr *e = in->p;
			Type *ot = e->ty, *tt = e->as.assign.target->ty;
			Value old = convert(R[in->a], tt, ot), res;
			if (e->as.assign.op == BIN_MOD && ot->kind == TY_FLOAT)
				res = v_float(fmod(old.as.f, R[in->b].as.f));
			else
				res = arith(e->as.assign.op, old, R[in->b], ot, e->loc);
			R[in->a] = convert(res, ot, tt);
			NEXT;
		}
		CASE(BC_ADDK_TO_I): {
			uint64_t x = (uint64_t)R[in->a].as.i, y = (uint64_t)in->v.as.i;
			uint64_t z = in->c == BIN_ADD ? x + y : in->c == BIN_SUB ? x - y : x * y;
			R[in->a] = v_int(bc_wrap((int64_t)z, in->k));
			NEXT;
		}
		CASE(BC_ADD_TO_I): {
			uint64_t x = (uint64_t)R[in->a].as.i, y = (uint64_t)R[in->b].as.i;
			uint64_t z = in->c == BIN_ADD ? x + y : in->c == BIN_SUB ? x - y : x * y;
			R[in->a] = v_int(bc_wrap((int64_t)z, in->k));
			NEXT;
		}
		CASE(BC_DROP):
			if (in->a || g_nlive_watches)
				drop_locals(I, in->p);
			NEXT;
		CASE(BC_TICK):   /* (counted here: Ctrl-C is the session's, which has no bytecode) */
			if (--g_turns <= 0)
				check_interrupt();
			NEXT;
		CASE(BC_SETRET): I->frame->ret = R[in->a]; NEXT;
		CASE(BC_RET):   return;
#ifndef BC_THREADED
		}
#endif
	}
}

#undef CASE
#undef NEXT
#ifdef BC_THREADED
#pragma GCC diagnostic pop
#endif
