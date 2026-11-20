/* ast_print.c — S-expression dump of the AST (tests and --dump-ast).
 * Expressions print on one line; statements one per line, indented two
 * spaces per level. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "parser.h"

static void indent(FILE *o, int n)
{
	for (int i = 0; i < n; i++)
		fputs("  ", o);
}

static void print_escaped(FILE *o, const char *s, size_t len, char quote)
{
	fputc(quote, o);
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)s[i];
		switch (c) {
		case '\n': fputs("\\n", o); break;
		case '\t': fputs("\\t", o); break;
		case '\r': fputs("\\r", o); break;
		case '\0': fputs("\\0", o); break;
		case '\\': fputs("\\\\", o); break;
		default:
			if (c == (unsigned char)quote)
				fprintf(o, "\\%c", c);
			else if (c < 0x20 || c == 0x7F)
				fprintf(o, "\\x%02X", c);
			else
				fputc(c, o);
		}
	}
	fputc(quote, o);
}

static void print_float(FILE *o, double v)
{
	char buf[64], first[64] = "";
	for (int prec = 1; prec <= 17; prec++) {
		snprintf(buf, sizeof buf, "%.*g", prec, v);
		if (strtod(buf, NULL) != v)
			continue;
		if (!strpbrk(buf, "eE"))
			break;
		if (!first[0])
			strcpy(first, buf);
	}
	if (strpbrk(buf, "eE") && first[0])
		strcpy(buf, first);
	fputs(buf, o);
	if (!strpbrk(buf, ".eEn"))
		fputs(".0", o);
}

void ast_print_type(FILE *o, const TypeExpr *t)
{
	if (!t) {
		fputs("auto", o);
		return;
	}
	switch (t->kind) {
	case TYPEX_NAMED: fputs(t->name, o); break;
	case TYPEX_ADDRESS:
		fputs("address ", o);
		ast_print_type(o, t->inner);
		break;
	case TYPEX_REF:
		fputs(t->is_const ? "const " : "", o);
		ast_print_type(o, t->inner);
		fputs("&", o);
		break;
	case TYPEX_ARRAY: {
		/* C order: element type, then dimensions outermost first */
		const TypeExpr *e = t;
		while (e->kind == TYPEX_ARRAY)
			e = e->inner;
		ast_print_type(o, e);
		for (const TypeExpr *d = t; d->kind == TYPEX_ARRAY; d = d->inner) {
			fputc('[', o);
			if (d->len)
				ast_print_expr(o, d->len);
			fputc(']', o);
		}
		break;
	}
	case TYPEX_PRESET:
		fputs("<type>", o);
		break;
	case TYPEX_SLICE:
		fputs(t->is_const ? "const " : "", o);
		ast_print_type(o, t->inner);
		fputs("[]", o);
		break;
	case TYPEX_LIST:
		fputs("list ", o);
		ast_print_type(o, t->inner);
		break;
	case TYPEX_LINK:
		fputs("link ", o);
		ast_print_type(o, t->inner);
		break;
	case TYPEX_LOOKUP:
		fputs("lookup ", o);
		ast_print_type(o, t->key);
		fputc(' ', o);
		ast_print_type(o, t->inner);
		break;
	}
}

static void print_args(FILE *o, const ExprVec *v)
{
	for (int i = 0; i < v->len; i++) {
		fputc(' ', o);
		ast_print_expr(o, v->items[i]);
	}
}

void ast_print_expr(FILE *o, const Expr *e)
{
	if (!e) {
		fputs("<null>", o);
		return;
	}
	switch (e->kind) {
	case EXPR_INT:
		fprintf(o, "%" PRIu64 "%s%s", e->as.int_lit.value, e->as.int_lit.is_unsigned ? "u" : "",
			e->as.int_lit.longs == 2 ? "ll" : e->as.int_lit.longs ? "l" : "");
		break;
	case EXPR_FLOAT:
		print_float(o, e->as.float_lit.value);
		if (e->as.float_lit.is_float32)
			fputc('f', o);
		break;
	case EXPR_BOOL:   fputs(e->as.bool_val ? "true" : "false", o); break;
	case EXPR_CHAR: {
		char c = (char)e->as.char_val;
		print_escaped(o, &c, 1, '\'');
		break;
	}
	case EXPR_STRING: print_escaped(o, e->as.str.data, e->as.str.len, '"'); break;
	case EXPR_IDENT:  fputs(e->as.ident.name, o); break;
	case EXPR_SELF:   fputs("self", o); break;
	case EXPR_UNARY:
		fprintf(o, "(%s ", ast_unop_str(e->as.unary.op));
		ast_print_expr(o, e->as.unary.operand);
		fputc(')', o);
		break;
	case EXPR_BINARY:
		fprintf(o, "(%s ", ast_binop_str(e->as.binary.op));
		ast_print_expr(o, e->as.binary.lhs);
		fputc(' ', o);
		ast_print_expr(o, e->as.binary.rhs);
		fputc(')', o);
		break;
	case EXPR_ASSIGN:
		if (e->as.assign.compound)
			fprintf(o, "(%s= ", ast_binop_str(e->as.assign.op));
		else
			fputs("(= ", o);
		ast_print_expr(o, e->as.assign.target);
		fputc(' ', o);
		ast_print_expr(o, e->as.assign.value);
		fputc(')', o);
		break;
	case EXPR_CALL:
		fputs(e->as.call.bare ? "(call! " : "(call ", o);
		ast_print_expr(o, e->as.call.callee);
		print_args(o, &e->as.call.args);
		fputc(')', o);
		break;
	case EXPR_FIELD:
		fputs("(. ", o);
		ast_print_expr(o, e->as.field.object);
		fprintf(o, " %s)", e->as.field.name);
		break;
	case EXPR_INDEX:
		if (e->as.index.is_slice) {
			fputs("([to] ", o);
			ast_print_expr(o, e->as.index.base);
			fputc(' ', o);
			if (e->as.index.index)
				ast_print_expr(o, e->as.index.index);
			else
				fputc('_', o);
			fputc(' ', o);
			if (e->as.index.to)
				ast_print_expr(o, e->as.index.to);
			else
				fputc('_', o);
			fputc(')', o);
			break;
		}
		fputs("([] ", o);
		ast_print_expr(o, e->as.index.base);
		fputc(' ', o);
		ast_print_expr(o, e->as.index.index);
		fputc(')', o);
		break;
	case EXPR_CAST:
		fputs(e->as.cast.implicit ? "(convert " : "(cast ", o);
		ast_print_type(o, e->as.cast.target);
		fputc(' ', o);
		ast_print_expr(o, e->as.cast.value);
		fputc(')', o);
		break;
	case EXPR_INIT:
		fputc('{', o);
		if (e->as.init.type) {
			ast_print_type(o, e->as.init.type);
			fputc(':', o);
		}
		for (int i = 0; i < e->as.init.entries.len; i++) {
			const InitEntry *en = &e->as.init.entries.items[i];
			fputs(i || e->as.init.type ? " " : "", o);
			if (en->field)
				fprintf(o, ".%s=", en->field);
			if (en->key) {
				ast_print_expr(o, en->key);
				fputc(':', o);
			}
			ast_print_expr(o, en->value);
		}
		fputc('}', o);
		break;
	case EXPR_SIZEOF:
		fputs("(sizeof ", o);
		if (e->as.size_of.type)
			ast_print_type(o, e->as.size_of.type);
		else
			ast_print_expr(o, e->as.size_of.expr);
		fputc(')', o);
		break;
	case EXPR_HANDLER:
		fprintf(o, "(handler %s)", e->as.handler.method->name);
		break;
	case EXPR_CALLBACK:
		fprintf(o, "(callback %s)", e->as.callback.fn ? e->as.callback.fn->name : "field");
		break;
	case EXPR_LINK_TO:
	case EXPR_LINK_GET:
		fputs(e->kind == EXPR_LINK_TO ? "(link-to " : "(link-get ", o);
		ast_print_expr(o, e->as.link.operand);
		fputc(')', o);
		break;
	case EXPR_AT:
		fputs("(at ", o);
		ast_print_expr(o, e->as.at.address);
		fputc(')', o);
		break;
	case EXPR_BORROW:
		fputs(e->as.borrow.is_mut ? "(&mut " : "(& ", o);
		ast_print_expr(o, e->as.borrow.operand);
		fputc(')', o);
		break;
	}
}

static void print_var(FILE *o, const VarDecl *v)
{
	fputs("(var ", o);
	if (v->is_static) fputs("static ", o);
	if (v->is_register) fputs("register ", o);
	if (v->is_volatile) fputs("volatile ", o);
	if (v->is_const) fputs("const ", o);
	ast_print_type(o, v->type);
	fprintf(o, " %s", v->name);
	if (v->init) {
		fputs(" = ", o);
		ast_print_expr(o, v->init);
	}
	fputc(')', o);
}

static void print_stmt(FILE *o, const Stmt *s, int ind);

static void print_block_body(FILE *o, const Block *b, int ind)
{
	if (!b)
		return;
	for (int i = 0; i < b->stmts.len; i++) {
		fputc('\n', o);
		print_stmt(o, b->stmts.items[i], ind);
	}
}

static void print_stmt(FILE *o, const Stmt *s, int ind)
{
	indent(o, ind);
	switch (s->kind) {
	case STMT_DECL:
		for (int i = 0; i < s->as.decl.len; i++) {
			if (i)
				fputc(' ', o);
			print_var(o, s->as.decl.items[i]);
		}
		break;
	case STMT_EXPR:
		ast_print_expr(o, s->as.expr);
		break;
	case STMT_IF:
		fputs("(if ", o);
		ast_print_expr(o, s->as.if_.cond);
		print_block_body(o, s->as.if_.then_block, ind + 1);
		if (s->as.if_.else_branch) {
			fputc('\n', o);
			indent(o, ind);
			if (s->as.if_.else_branch->kind == STMT_IF) {
				fputs(" else\n", o);
				print_stmt(o, s->as.if_.else_branch, ind + 1);
			} else {
				fputs(" else", o);
				print_block_body(o, s->as.if_.else_branch->as.block, ind + 1);
			}
		}
		fputc(')', o);
		break;
	case STMT_WHILE:
	case STMT_DO:
		fputs(s->kind == STMT_DO ? "(do-while " : "(while ", o);
		ast_print_expr(o, s->as.while_.cond);
		print_block_body(o, s->as.while_.body, ind + 1);
		fputc(')', o);
		break;
	case STMT_FOR:
		if (s->as.for_.range) {
			const ForRange *r = s->as.for_.range;
			fputs("(for-in ", o);
			if (r->var->type) {
				ast_print_type(o, r->var->type);
				fputc(' ', o);
			}
			fprintf(o, "%s ", r->var->name);
			if (r->start) {
				ast_print_expr(o, r->start);
				fputs(r->down ? " down-to " : " to ", o);
			}
			ast_print_expr(o, r->end);
			if (r->step) {
				fputs(" by ", o);
				ast_print_expr(o, r->step);
			}
			print_block_body(o, s->as.for_.body, ind + 1);
			fputc(')', o);
			break;
		}
		fputs("(for (", o);
		if (s->as.for_.init)
			print_stmt(o, s->as.for_.init, 0);
		fputs(") (", o);
		if (s->as.for_.cond)
			ast_print_expr(o, s->as.for_.cond);
		fputs(") (", o);
		if (s->as.for_.update)
			print_stmt(o, s->as.for_.update, 0);
		fputc(')', o);
		print_block_body(o, s->as.for_.body, ind + 1);
		fputc(')', o);
		break;
	case STMT_SWITCH:
		fputs("(switch ", o);
		ast_print_expr(o, s->as.switch_.value);
		for (int i = 0; i < s->as.switch_.cases.len; i++) {
			const SwitchCase *c = &s->as.switch_.cases.items[i];
			fputc('\n', o);
			indent(o, ind + 1);
			if (c->is_default)
				fputs("(default", o);
			else {
				fputs("(case", o);
				print_args(o, &c->values);
			}
			print_block_body(o, c->body, ind + 2);
			fputc(')', o);
		}
		fputc(')', o);
		break;
	case STMT_RETURN:
		fputs("(return", o);
		if (s->as.ret.value) {
			fputc(' ', o);
			ast_print_expr(o, s->as.ret.value);
		}
		fputc(')', o);
		break;
	case STMT_BREAK:    fputs("(break)", o); break;
	case STMT_CONTINUE: fputs("(continue)", o); break;
	case STMT_GOTO:     fprintf(o, "(goto %s)", s->as.goto_.label); break;
	case STMT_LABEL:    fprintf(o, "(label %s)", s->as.label.name); break;
	case STMT_BLOCK:
		fputs("(block", o);
		print_block_body(o, s->as.block, ind + 1);
		fputc(')', o);
		break;
	}
	if (s->recover) {
		fputs(" (else", o);
		if (s->recover->why)
			fprintf(o, " %s", s->recover->why);
		if (s->recover->value) {
			fputc(' ', o);
			ast_print_expr(o, s->recover->value);
		}
		if (s->recover->block)
			print_block_body(o, s->recover->block, ind + 1);
		fputc(')', o);
	}
}

static void print_fn(FILE *o, const FnDecl *fn, int ind)
{
	indent(o, ind);
	fprintf(o, "(%s%s%s ", fn->is_extern ? "extern " : "", fn->is_static ? "static " : "",
		fn->is_prototype && !fn->is_extern ? "proto" : "fn");
	ast_print_type(o, fn->ret);
	fprintf(o, " %s (", fn->name);
	for (int i = 0; i < fn->params.len; i++) {
		if (i)
			fputs(", ", o);
		const VarDecl *p = fn->params.items[i];
		if (p->is_const)
			fputs("const ", o);
		ast_print_type(o, p->type);
		fprintf(o, " %s", p->name);
	}
	if (fn->is_variadic)
		fputs(fn->params.len ? ", ..." : "...", o);
	fputc(')', o);
	if (fn->owner && fn->self_kind == SELF_REF)
		fputs(" const", o);
	print_block_body(o, fn->body, ind + 1);
	fputc(')', o);
}

void ast_print_program(FILE *o, const Program *prog)
{
	for (int i = 0; i < prog->decls.len; i++) {
		const Decl *d = prog->decls.items[i];
		switch (d->kind) {
		case DECL_FN:
			print_fn(o, d->as.fn, 0);
			break;
		case DECL_OBJECT: {
			const ObjectDecl *obj = d->as.object;
			fprintf(o, "(%s %s", ast_object_kind_name(obj->kind), obj->name);
			if (obj->parents.len) {
				fputs(" (parents", o);
				for (int j = 0; j < obj->parents.len; j++)
					fprintf(o, " %s", obj->parents.items[j].name);
				fputc(')', o);
			}
			for (int j = 0; j < obj->fields.len; j++) {
				fputs("\n  ", o);
				print_var(o, obj->fields.items[j]);
			}
			for (int j = 0; j < obj->methods.len; j++) {
				fputc('\n', o);
				print_fn(o, obj->methods.items[j], 1);
			}
			fputc(')', o);
			break;
		}
		case DECL_ENUM: {
			const EnumDecl *en = d->as.enum_;
			fprintf(o, "(enum %s", en->name);
			for (int j = 0; j < en->items.len; j++) {
				fprintf(o, " %s", en->items.items[j].name);
				if (en->items.items[j].value) {
					fputc('=', o);
					ast_print_expr(o, en->items.items[j].value);
				}
			}
			fputc(')', o);
			break;
		}
		case DECL_TYPEDEF:
			fprintf(o, "(typedef %s ", d->as.typedef_.name);
			ast_print_type(o, d->as.typedef_.type);
			fputc(')', o);
			break;
		case DECL_VAR:
			fputs(d->is_static ? "(global static" : "(global", o);
			for (int j = 0; j < d->as.vars.len; j++) {
				fputc(' ', o);
				print_var(o, d->as.vars.items[j]);
			}
			fputc(')', o);
			break;
		}
		fputc('\n', o);
	}
}
