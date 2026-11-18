#include <string.h>
#include "ast.h"

TypeExpr *ast_type_expr(Arena *a, TypeExprKind kind, SrcLoc loc)
{
	TypeExpr *t = arena_alloc(a, sizeof *t);
	t->kind = kind;
	t->loc = loc;
	return t;
}

TypeExpr *ast_type_named(Arena *a, const char *name, bool builtin, SrcLoc loc)
{
	TypeExpr *t = ast_type_expr(a, TYPEX_NAMED, loc);
	t->name = name;
	t->builtin = builtin;
	return t;
}

Expr *ast_expr(Arena *a, ExprKind kind, SrcLoc loc)
{
	Expr *e = arena_alloc(a, sizeof *e);
	e->kind = kind;
	e->loc = loc;
	if (kind == EXPR_FIELD) {
		e->as.field.field_index = -1;
		e->as.field.owner_id = -1;
	}
	if (kind == EXPR_CALL)
		e->as.call.slot = -1;
	return e;
}

Stmt *ast_stmt(Arena *a, StmtKind kind, SrcLoc loc)
{
	Stmt *s = arena_alloc(a, sizeof *s);
	s->kind = kind;
	s->loc = loc;
	if (kind == STMT_SWITCH)
		s->as.switch_.default_index = -1;
	return s;
}

Block *ast_block(Arena *a, SrcLoc loc)
{
	Block *b = arena_alloc(a, sizeof *b);
	b->loc = loc;
	return b;
}

VarDecl *ast_var(Arena *a, const char *name, SrcLoc loc)
{
	VarDecl *v = arena_alloc(a, sizeof *v);
	v->name = name;
	v->loc = loc;
	return v;
}

FnDecl *ast_fn(Arena *a, const char *name, SrcLoc loc)
{
	FnDecl *f = arena_alloc(a, sizeof *f);
	f->name = name;
	f->loc = loc;
	f->vtable_slot = -1;
	return f;
}

ObjectDecl *ast_object(Arena *a, ObjectKind kind, const char *name, SrcLoc loc)
{
	ObjectDecl *o = arena_alloc(a, sizeof *o);
	o->kind = kind;
	o->name = name;
	o->loc = loc;
	return o;
}

Decl *ast_decl(Arena *a, DeclKind kind, SrcLoc loc)
{
	Decl *d = arena_alloc(a, sizeof *d);
	d->kind = kind;
	d->loc = loc;
	return d;
}

const char *ast_binop_str(BinaryOp op)
{
	static const char *s[BIN__COUNT] = {
		[BIN_ADD] = "+",  [BIN_SUB] = "-",  [BIN_MUL] = "*", [BIN_DIV] = "/", [BIN_MOD] = "%",
		[BIN_BIT_AND] = "&", [BIN_BIT_OR] = "|", [BIN_BIT_XOR] = "^",
		[BIN_SHL] = "<<", [BIN_SHR] = ">>",
		[BIN_LOG_AND] = "&&", [BIN_LOG_OR] = "||",
		[BIN_EQ] = "==", [BIN_NE] = "!=", [BIN_LT] = "<", [BIN_LE] = "<=",
		[BIN_GT] = ">",  [BIN_GE] = ">=",
	};
	return (op >= 0 && op < BIN__COUNT) ? s[op] : "?";
}

const char *ast_unop_str(UnaryOp op)
{
	switch (op) {
	case UN_NEG:    return "-";
	case UN_NOT:    return "!";
	case UN_BITNOT: return "~";
	}
	return "?";
}

bool ast_binop_is_logical(BinaryOp op) { return op == BIN_LOG_AND || op == BIN_LOG_OR; }
bool ast_binop_is_bitwise(BinaryOp op) { return op >= BIN_BIT_AND && op <= BIN_SHR; }
bool ast_binop_is_comparison(BinaryOp op) { return op >= BIN_EQ && op <= BIN_GE; }
bool ast_binop_is_arith(BinaryOp op) { return op <= BIN_MOD; }

const char *ast_object_kind_name(ObjectKind k)
{
	switch (k) {
	case OBJ_OBJECT: return "object";
	case OBJ_STRUCT: return "struct";
	case OBJ_UNION:  return "union";
	}
	return "?";
}

/* A deep copy of an unchecked expression (TypeExprs are shared). */
Expr *ast_clone_expr(Arena *a, const Expr *e)
{
	if (!e)
		return NULL;
	Expr *c = arena_alloc(a, sizeof *c);
	*c = *e;
	switch (e->kind) {
	case EXPR_UNARY:
		c->as.unary.operand = ast_clone_expr(a, e->as.unary.operand);
		break;
	case EXPR_BINARY:
		c->as.binary.lhs = ast_clone_expr(a, e->as.binary.lhs);
		c->as.binary.rhs = ast_clone_expr(a, e->as.binary.rhs);
		break;
	case EXPR_ASSIGN:
		c->as.assign.target = ast_clone_expr(a, e->as.assign.target);
		c->as.assign.value = ast_clone_expr(a, e->as.assign.value);
		break;
	case EXPR_CALL:
		c->as.call.callee = ast_clone_expr(a, e->as.call.callee);
		memset(&c->as.call.args, 0, sizeof c->as.call.args);
		for (int i = 0; i < e->as.call.args.len; i++)
			vec_push(a, c->as.call.args, ast_clone_expr(a, e->as.call.args.items[i]));
		break;
	case EXPR_FIELD:
		c->as.field.object = ast_clone_expr(a, e->as.field.object);
		break;
	case EXPR_INDEX:
		c->as.index.base = ast_clone_expr(a, e->as.index.base);
		c->as.index.index = ast_clone_expr(a, e->as.index.index);
		c->as.index.to = ast_clone_expr(a, e->as.index.to);
		break;
	case EXPR_CAST:
		c->as.cast.value = ast_clone_expr(a, e->as.cast.value);
		break;
	case EXPR_INIT:
		memset(&c->as.init.entries, 0, sizeof c->as.init.entries);
		for (int i = 0; i < e->as.init.entries.len; i++) {
			InitEntry en = e->as.init.entries.items[i];
			en.value = ast_clone_expr(a, en.value);
			en.key = ast_clone_expr(a, en.key);
			vec_push(a, c->as.init.entries, en);
		}
		break;
	case EXPR_SIZEOF:
		c->as.size_of.expr = ast_clone_expr(a, e->as.size_of.expr);
		break;
	case EXPR_BORROW:
		c->as.borrow.operand = ast_clone_expr(a, e->as.borrow.operand);
		break;
	default:
		break;
	}
	return c;
}
