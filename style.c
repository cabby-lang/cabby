/* style.c — `cabby --style`: names, and calls, that don't follow STYLE.md.
 *
 *   functions, methods, types, labels    snake_case     do_this, point
 *   variables, parameters, fields        PascalCase     DateOfBirth
 *                                        (or one lower-case letter: i, x)
 *   constants (anywhere), enum values    UPPER_CASE     DAYS_PER_WEEK
 *
 * And parentheses a call doesn't need (style_calls, before the checker):
 *
 *   f()                         no empty parentheses: `f`
 *   f(a, b)  alone on a line    `f a, b`
 *   int X = f(a)                the whole value of a declaration, an
 *   X = f(a)    return f(a)     assignment or a return: `f a`
 *   if f(a) {   for X in f(a) { the whole condition, or what a for goes
 *                               through: `if f a {`
 *
 * Only the program's own files are looked at: library headers (`use gui`)
 * and C functions declared with `extern` keep the names they have. Each
 * warning suggests the name or call written the right way. */
#include "style.h"

#include "diag.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Style {
	const Program *prog;
	bool *ours;      /* by file id: one of the program's own files */
	int nfiles;
	int count;
} Style;

/* ---------------------------------------------------------------- names */

static bool is_snake(const char *s)
{
	if (!islower((unsigned char)*s))
		return false;
	for (const char *p = s; *p; p++) {
		if (*p == '_') {
			if (p[1] == '\0' || p[1] == '_')
				return false;
		} else if (!islower((unsigned char)*p) && !isdigit((unsigned char)*p)) {
			return false;
		}
	}
	return true;
}

/* A type may be namespaced: gui.window */
static bool is_snake_dotted(const char *s)
{
	char buf[256];
	size_t n = strlen(s);
	if (n >= sizeof buf)
		return true;
	memcpy(buf, s, n + 1);
	for (char *part = strtok(buf, "."); part; part = strtok(NULL, "."))
		if (!is_snake(part))
			return false;
	return true;
}

static bool is_upper(const char *s)
{
	if (!isupper((unsigned char)*s))
		return false;
	for (const char *p = s; *p; p++) {
		if (*p == '_') {
			if (p[1] == '\0' || p[1] == '_')
				return false;
		} else if (!isupper((unsigned char)*p) && !isdigit((unsigned char)*p)) {
			return false;
		}
	}
	return true;
}

/* PascalCase: a capital first, no underscores, and not all capitals (that
 * would read as a constant). One lower-case letter, perhaps with digits
 * (i, x, y2), is allowed for counters and coordinates. */
static bool is_pascal(const char *s)
{
	if (islower((unsigned char)s[0])) {
		for (const char *p = s + 1; *p; p++)
			if (!isdigit((unsigned char)*p))
				return false;
		return true;
	}
	if (!isupper((unsigned char)s[0]) || strchr(s, '_'))
		return false;
	bool lower = false;
	for (const char *p = s; *p; p++) {
		if (!isalnum((unsigned char)*p))
			return false;
		if (islower((unsigned char)*p))
			lower = true;
	}
	return lower || s[1] == '\0';
}

/* Splits a name into lower-case words: date_of_birth, DateOfBirth,
 * DATE_OF_BIRTH and HTTPStatus all give their words. */
static int words_of(const char *s, char words[][64], int max)
{
	int n = 0;
	size_t len = strlen(s);
	char cur[64];
	int cl = 0;
	for (size_t i = 0; i <= len; i++) {
		char c = s[i];
		bool split = false;
		if (c == '\0' || c == '_' || c == '.') {
			split = true;
		} else if (cl > 0 && isupper((unsigned char)c)) {
			char prev = s[i - 1];
			char next = s[i + 1];
			/* a new word at a capital after a small letter or digit, or at
			 * the last capital of a run followed by a small letter (HTTPStatus) */
			if (islower((unsigned char)prev) || isdigit((unsigned char)prev) ||
			    (isupper((unsigned char)prev) && islower((unsigned char)next)))
				split = true;
		}
		if (split && cl > 0) {
			if (n < max) {
				cur[cl] = '\0';
				memcpy(words[n++], cur, (size_t)cl + 1);
			}
			cl = 0;
		}
		if (c != '\0' && c != '_' && c != '.' && cl < 63)
			cur[cl++] = (char)tolower((unsigned char)c);
	}
	return n;
}

typedef enum { CASE_SNAKE, CASE_PASCAL, CASE_UPPER } Case;

static void suggest(const char *name, Case c, char *out, size_t size)
{
	char words[32][64];
	int n = words_of(name, words, 32);
	size_t o = 0;
	for (int i = 0; i < n && o + 1 < size; i++) {
		if (i > 0 && c != CASE_PASCAL && o + 1 < size)
			out[o++] = '_';
		for (int k = 0; words[i][k] && o + 1 < size; k++) {
			char ch = words[i][k];
			if (c == CASE_UPPER || (c == CASE_PASCAL && k == 0))
				ch = (char)toupper((unsigned char)ch);
			out[o++] = ch;
		}
	}
	out[o] = '\0';
}

static const char *rule_for(Case c)
{
	return c == CASE_SNAKE ? "lower case, with '_' between words"
	     : c == CASE_PASCAL ? "a capital at the start of each word"
	     : "capitals, with '_' between words";
}

static void check_name(Style *S, const char *name, SrcLoc loc, const char *what, Case c, bool dotted)
{
	if (!name || !*name || strchr(name, ' '))   /* the checker's own hidden variables */
		return;
	if (loc.file < 0 || loc.file >= S->nfiles || !S->ours[loc.file])
		return;
	bool ok = c == CASE_SNAKE ? (dotted ? is_snake_dotted(name) : is_snake(name))
		: c == CASE_PASCAL ? is_pascal(name)
		: is_upper(name);
	if (ok)
		return;
	char better[256];
	suggest(name, c, better, sizeof better);
	if (dotted && c == CASE_SNAKE) {
		/* keep the namespace's dots */
		char buf[256];
		size_t o = 0;
		char copy[256];
		snprintf(copy, sizeof copy, "%s", name);
		for (char *part = strtok(copy, "."); part && o < sizeof buf - 1; part = strtok(NULL, ".")) {
			char p2[256];
			suggest(part, CASE_SNAKE, p2, sizeof p2);
			o += (size_t)snprintf(buf + o, sizeof buf - o, "%s%s", o ? "." : "", p2);
		}
		snprintf(better, sizeof better, "%s", buf);
	}
	if (!*better || strcmp(better, name) == 0)
		return;
	S->count++;
	diag_report_in(DIAG_WARNING, loc.file, loc.line, loc.col, "style: write %s '%s' as '%s' (%s)",
		       what, name, better, rule_for(c));
}

/* ---------------------------------------------------------------- walking */

static void walk_block(Style *S, const Block *b);

static void walk_var(Style *S, const VarDecl *v, const char *what)
{
	check_name(S, v->name, v->loc, what, CASE_PASCAL, false);
}

static void walk_block(Style *S, const Block *b);

static void walk_stmt(Style *S, const Stmt *s)
{
	if (!s)
		return;
	if (s->recover && s->recover->block)
		walk_block(S, s->recover->block);
	switch (s->kind) {
	case STMT_DECL:
		for (int i = 0; i < s->as.decl.len; i++) {
			const VarDecl *v = s->as.decl.items[i];
			/* a constant is in capitals wherever it is; a read-only borrow
			 * (`const T &Name`) is a variable that can't change its target */
			bool borrow = v->type && (v->type->kind == TYPEX_REF || v->type->kind == TYPEX_SLICE);
			if (v->is_const && !borrow)
				check_name(S, v->name, v->loc, "the constant", CASE_UPPER, false);
			else
				walk_var(S, v, "the variable");
		}
		break;
	case STMT_IF:
		walk_block(S, s->as.if_.then_block);
		walk_stmt(S, s->as.if_.else_branch);
		break;
	case STMT_WHILE:
	case STMT_DO:
		walk_block(S, s->as.while_.body);
		break;
	case STMT_FOR: {
		const Block *body = s->as.for_.body;
		int first = 0;
		if (s->as.for_.range && s->as.for_.range->var) {
			walk_var(S, s->as.for_.range->var, "the counter");
			if (s->as.for_.range->var2)
				walk_var(S, s->as.for_.range->var2, "the counter");
			/* `for X in Things` (or `for K, V in Lookup`): the checker declared X
			 * (or K and V) at the top of the body */
			const VarDecl *x = s->as.for_.range->var;
			if (body && body->stmts.len > 0 && body->stmts.items[0]->kind == STMT_DECL &&
			    body->stmts.items[0]->as.decl.len == (s->as.for_.range->var2 ? 2 : 1) &&
			    body->stmts.items[0]->as.decl.items[0]->loc.line == x->loc.line &&
			    body->stmts.items[0]->as.decl.items[0]->loc.col == x->loc.col)
				first = 1;
		} else {
			walk_stmt(S, s->as.for_.init);   /* (a for-in's init is the checker's own) */
		}
		for (int i = first; body && i < body->stmts.len; i++)
			walk_stmt(S, body->stmts.items[i]);
		break;
	}
	case STMT_SWITCH:
		for (int i = 0; i < s->as.switch_.cases.len; i++)
			walk_block(S, s->as.switch_.cases.items[i].body);
		break;
	case STMT_LABEL:
		check_name(S, s->as.label.name, s->loc, "the label", CASE_SNAKE, false);
		break;
	case STMT_BLOCK:
		walk_block(S, s->as.block);
		break;
	default:
		break;
	}
}

static void walk_block(Style *S, const Block *b)
{
	if (!b)
		return;
	for (int i = 0; i < b->stmts.len; i++)
		walk_stmt(S, b->stmts.items[i]);
}

static void walk_fn(Style *S, const FnDecl *fn, bool method)
{
	if (fn->is_extern || fn->is_destructor || fn->is_maker)
		return;
	if (strcmp(fn->name, "main") != 0)
		check_name(S, fn->name, fn->loc, method ? "the method" : "the function", CASE_SNAKE, true);
	for (int i = 0; i < fn->params.len; i++)
		walk_var(S, fn->params.items[i], "the parameter");
	walk_block(S, fn->body);
}

/* Is this file one of the program's own (not a library from `use name`)? */
static bool own_file(const Program *prog, const char *path)
{
	if (path[0] == '(')
		return false;   /* written by Cabby: "(json for structs).hby" */
	const char *slash = strrchr(path, '/');
	size_t dirlen = slash ? (size_t)(slash - path) : 0;
	const char *base = slash ? slash + 1 : path;
	for (int i = 0; i < prog->natives.len; i++) {
		const NativeLib *nl = &prog->natives.items[i];
		if (nl->source)
			continue;   /* (use local, with a .c: the header's one's own) */
		size_t nlen = strlen(nl->name);
		if (strlen(nl->dir) == dirlen && strncmp(nl->dir, path, dirlen) == 0 &&
		    strncmp(base, nl->name, nlen) == 0 && (strcmp(base + nlen, ".hby") == 0 || strcmp(base + nlen, ".cby") == 0))
			return false;
	}
	return true;
}

int style_check(const Program *prog)
{
	Style S = { .prog = prog };
	int maxid = 0;
	for (int i = 0; i < prog->files.len; i++)
		if (prog->files.items[i].id > maxid)
			maxid = prog->files.items[i].id;
	S.nfiles = maxid + 1;
	S.ours = calloc((size_t)S.nfiles, sizeof *S.ours);
	for (int i = 0; i < prog->files.len; i++)
		S.ours[prog->files.items[i].id] = own_file(prog, prog->files.items[i].path);

	for (int i = 0; i < prog->decls.len; i++) {
		const Decl *d = prog->decls.items[i];
		switch (d->kind) {
		case DECL_FN:
			walk_fn(&S, d->as.fn, false);
			break;
		case DECL_OBJECT: {
			const ObjectDecl *o = d->as.object;
			const char *kind = o->kind == OBJ_OBJECT ? "the object" : o->kind == OBJ_STRUCT ? "the struct" : "the union";
			check_name(&S, o->name, o->loc, kind, CASE_SNAKE, true);
			for (int k = 0; k < o->fields.len; k++)
				walk_var(&S, o->fields.items[k], "the field");
			for (int k = 0; k < o->methods.len; k++)
				walk_fn(&S, o->methods.items[k], true);
			break;
		}
		case DECL_ENUM: {
			const EnumDecl *e = d->as.enum_;
			check_name(&S, e->name, e->loc, "the enum", CASE_SNAKE, true);
			for (int k = 0; k < e->items.len; k++)
				check_name(&S, e->items.items[k].name, e->items.items[k].loc, "the enum value", CASE_UPPER, false);
			break;
		}
		case DECL_TYPEDEF:
			check_name(&S, d->as.typedef_.name, d->loc, "the type", CASE_SNAKE, true);
			break;
		case DECL_VAR:
			for (int k = 0; k < d->as.vars.len; k++) {
				const VarDecl *v = d->as.vars.items[k];
				if (v->is_const)
					check_name(&S, v->name, v->loc, "the constant", CASE_UPPER, false);
				else
					walk_var(&S, v, "the global variable");
			}
			break;
		}
	}
	free(S.ours);
	return S.count;
}

/* ---------------------------------------------------------------- calls */

typedef struct Calls {
	const Program *prog;
	bool *ours;
	const char **src;   /* by file id */
	int nfiles;
	int count;
} Calls;

/* The text of a call's own parentheses, from the source: where the `(`
 * is, and what's between it and its `)`. false if they can't be found on
 * the call's line. */
static bool call_parens(Calls *K, const Expr *call, int *col, char *inside, size_t size)
{
	SrcLoc l = call->as.call.callee->loc;
	if (l.file < 0 || l.file >= K->nfiles || !K->src[l.file])
		return false;
	const char *p = K->src[l.file];
	for (int line = 1; line < l.line && *p; p++)
		if (*p == '\n')
			line++;
	const char *start = p;
	const char *q = p + (l.col > 0 ? l.col - 1 : 0);
	while (*q && *q != '\n' && *q != '(' && *q != '"')
		q++;
	if (*q != '(')
		return false;
	*col = (int)(q - start) + 1;
	int depth = 0;
	const char *a = q + 1;
	for (const char *r = q; *r && *r != '\n'; r++) {
		if (*r == '"' || *r == '\'') {
			char quote = *r;
			for (r++; *r && *r != quote && *r != '\n'; r++)
				if (*r == '\\' && r[1])
					r++;
			if (*r != quote)
				return false;
			continue;
		}
		if (*r == '(')
			depth++;
		else if (*r == ')' && --depth == 0) {
			size_t n = (size_t)(r - a);
			if (n >= size)
				return false;
			memcpy(inside, a, n);
			inside[n] = '\0';
			return true;
		}
	}
	return false;
}

static const char *call_name(const Expr *call)
{
	const Expr *c = call->as.call.callee;
	return c->kind == EXPR_IDENT ? c->as.ident.name : c->kind == EXPR_FIELD ? c->as.field.name : NULL;
}

/* Every call written with empty parentheses, anywhere in e */
static void empty_parens(Calls *K, const Expr *e)
{
	if (!e)
		return;
	switch (e->kind) {
	case EXPR_CALL: {
		const char *name = call_name(e);
		char inside[8];
		int col;
		if (!e->as.call.bare && e->as.call.args.len == 0 && name && call_parens(K, e, &col, inside, sizeof inside)) {
			SrcLoc l = e->as.call.callee->loc;
			if (l.file >= 0 && l.file < K->nfiles && K->ours[l.file]) {
				K->count++;
				diag_report_in(DIAG_WARNING, l.file, l.line, col, "style: write '%s' without '()': a call "
					       "with nothing to give it is written by its name", name);
			}
		}
		empty_parens(K, e->as.call.callee);
		for (int i = 0; i < e->as.call.args.len; i++)
			empty_parens(K, e->as.call.args.items[i]);
		break;
	}
	case EXPR_UNARY:   empty_parens(K, e->as.unary.operand); break;
	case EXPR_BINARY:  empty_parens(K, e->as.binary.lhs); empty_parens(K, e->as.binary.rhs); break;
	case EXPR_ASSIGN:  empty_parens(K, e->as.assign.target); empty_parens(K, e->as.assign.value); break;
	case EXPR_FIELD:   empty_parens(K, e->as.field.object); break;
	case EXPR_INDEX:
		empty_parens(K, e->as.index.base);
		empty_parens(K, e->as.index.index);
		empty_parens(K, e->as.index.to);
		break;
	case EXPR_CAST:    empty_parens(K, e->as.cast.value); break;
	case EXPR_INIT:
		for (int i = 0; i < e->as.init.entries.len; i++) {
			empty_parens(K, e->as.init.entries.items[i].key);
			empty_parens(K, e->as.init.entries.items[i].value);
		}
		break;
	default:
		break;
	}
}

/* A call that is a whole value (`where` says which): with arguments in
 * parentheses it needs none. Not when the first argument would read
 * differently without them: `f(-x)` (f - x), `f({1, 2})`, and one that
 * starts with a parenthesis of its own, `f((int)x, y)` or `f((a + b) * 2)`
 * (`f (int)x` would be f's arguments in parentheses). */
static void whole_value(Calls *K, const Expr *e, const char *where)
{
	if (!e || e->kind != EXPR_CALL || e->as.call.bare || e->as.call.args.len == 0 || e->parenthesized)
		return;
	const Expr *first = e->as.call.args.items[0];
	if ((first->kind == EXPR_UNARY && first->as.unary.op == UN_NEG) || first->kind == EXPR_INIT)
		return;
	const char *name = call_name(e);
	SrcLoc l = e->as.call.callee->loc;
	if (!name || l.file < 0 || l.file >= K->nfiles || !K->ours[l.file])
		return;
	char inside[200];
	int col;
	if (!call_parens(K, e, &col, inside, sizeof inside) || inside[0] == '(')
		return;
	K->count++;
	diag_report_in(DIAG_WARNING, l.file, l.line, col, "style: write '%s %s' without parentheses: a call that is %s "
		       "needs none", name, inside, where);
}

static void calls_block(Calls *K, const Block *b);

static void calls_block(Calls *K, const Block *b);

static void calls_stmt(Calls *K, const Stmt *s)
{
	if (!s)
		return;
	if (s->recover) {
		if (s->recover->value) {
			whole_value(K, s->recover->value, "the whole value after 'else'");
			empty_parens(K, s->recover->value);
		}
		if (s->recover->block)
			calls_block(K, s->recover->block);
	}
	switch (s->kind) {
	case STMT_DECL:
		for (int i = 0; i < s->as.decl.len; i++) {
			const Expr *init = s->as.decl.items[i]->init;
			whole_value(K, init, "the whole value of a declaration");
			empty_parens(K, init);
		}
		break;
	case STMT_EXPR: {
		const Expr *e = s->as.expr;
		if (e && e->kind == EXPR_ASSIGN)
			whole_value(K, e->as.assign.value, "the whole value of an assignment");
		else
			whole_value(K, e, "a line of its own");
		empty_parens(K, e);
		break;
	}
	case STMT_RETURN:
		whole_value(K, s->as.ret.value, "the whole value of a return");
		empty_parens(K, s->as.ret.value);
		break;
	case STMT_IF:
		whole_value(K, s->as.if_.cond, "the whole condition");
		empty_parens(K, s->as.if_.cond);
		calls_block(K, s->as.if_.then_block);
		calls_stmt(K, s->as.if_.else_branch);
		break;
	case STMT_WHILE:
	case STMT_DO:
		if (s->kind == STMT_WHILE)
			whole_value(K, s->as.while_.cond, "the whole condition");
		empty_parens(K, s->as.while_.cond);
		calls_block(K, s->as.while_.body);
		break;
	case STMT_FOR:
		if (s->as.for_.range) {
			const ForRange *r = s->as.for_.range;
			if (!r->start)
				whole_value(K, r->end, "what a for goes through");
			empty_parens(K, r->start);
			empty_parens(K, r->end);
			empty_parens(K, r->step);
		} else {
			calls_stmt(K, s->as.for_.init);
			empty_parens(K, s->as.for_.cond);
			calls_stmt(K, s->as.for_.update);
		}
		calls_block(K, s->as.for_.body);
		break;
	case STMT_SWITCH:
		empty_parens(K, s->as.switch_.value);
		for (int i = 0; i < s->as.switch_.cases.len; i++)
			calls_block(K, s->as.switch_.cases.items[i].body);
		break;
	case STMT_BLOCK:
		calls_block(K, s->as.block);
		break;
	default:
		break;
	}
}

static void calls_block(Calls *K, const Block *b)
{
	if (!b)
		return;
	for (int i = 0; i < b->stmts.len; i++)
		calls_stmt(K, b->stmts.items[i]);
}

int style_calls(const Program *prog)
{
	Calls K = { .prog = prog };
	int maxid = 0;
	for (int i = 0; i < prog->files.len; i++)
		if (prog->files.items[i].id > maxid)
			maxid = prog->files.items[i].id;
	K.nfiles = maxid + 1;
	K.ours = calloc((size_t)K.nfiles, sizeof *K.ours);
	K.src = calloc((size_t)K.nfiles, sizeof *K.src);
	for (int i = 0; i < prog->files.len; i++) {
		K.ours[prog->files.items[i].id] = own_file(prog, prog->files.items[i].path);
		K.src[prog->files.items[i].id] = prog->files.items[i].src;
	}
	for (int i = 0; i < prog->decls.len; i++) {
		const Decl *d = prog->decls.items[i];
		if (d->kind == DECL_FN && d->as.fn->body) {
			calls_block(&K, d->as.fn->body);
		} else if (d->kind == DECL_OBJECT) {
			const ObjectDecl *o = d->as.object;
			for (int k = 0; k < o->methods.len; k++)
				calls_block(&K, o->methods.items[k]->body);
		} else if (d->kind == DECL_VAR) {
			for (int k = 0; k < d->as.vars.len; k++) {
				whole_value(&K, d->as.vars.items[k]->init, "the whole value of a declaration");
				empty_parens(&K, d->as.vars.items[k]->init);
			}
		}
	}
	free(K.ours);
	free(K.src);
	return K.count;
}

