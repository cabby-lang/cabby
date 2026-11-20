/* sema_int.h — state and helpers shared by the sema_*.c files. */
#ifndef SEMA_INT_H
#define SEMA_INT_H

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "sema.h"

typedef struct Scope {
	struct Scope *parent;
	SymbolVec syms;
} Scope;

typedef struct LoopInfo {
	struct LoopInfo *parent;
	int start, end;
} LoopInfo;

typedef enum { ACC_READ, ACC_WRITE, ACC_MOVE, ACC_BORROW, ACC_BORROW_MUT } AccessKind;

typedef struct {
	Symbol *sym;
	AccessKind kind;
	int pos;
	SrcLoc loc;
	const char *path;    /* the part of sym: "" all of it, "Things", "Shown.Id" (fields) */
} Access;

typedef struct {
	Symbol *root;        /* the variable whose contents are borrowed */
	const char *path;    /* which part of it: "" all of it, or fields ("Things") */
	bool is_mut;
	int created;         /* position of the borrow */
	int start, end;      /* live range */
	Symbol *borrower;    /* named reference holding the loan; NULL for a temporary */
	LoopInfo *loop;      /* loop the loan must span (borrower declared outside it) */
	SrcLoc loc;
} Loan;

typedef VEC(Access) AccessVec;
typedef VEC(Loan) LoanVec;
typedef VEC(int) IntVec;

typedef enum {
	CTX_VALUE,  /* consumed: Copy types copy, others move */
	CTX_READ,   /* inspected in place (comparisons, printing, field base) */
	CTX_PLACE,  /* evaluated as a place; the caller records the access */
} Ctx;

/* Value-namespace entries visible at top level. */
typedef enum { GN_FN, GN_GLOBAL, GN_ENUM_CONST } GlobalKind;

typedef struct {
	const char *name;
	GlobalKind kind;
	FnDecl *fn;          /* GN_FN: the definition if any, else the first declaration */
	FnDecl *decl;        /* GN_FN: first declaration seen (prototype or definition) */
	Symbol *sym;         /* GN_GLOBAL */
	int64_t value;       /* GN_ENUM_CONST */
	Type *type;          /* GN_ENUM_CONST */
	int file;
	bool file_static;
	SrcLoc loc;
} GlobalName;

/* Type-namespace entries. */
typedef struct {
	const char *name;
	Type *type;          /* resolved (NULL while a typedef is being resolved) */
	TypeExpr *te;        /* typedef: the aliased type expression */
	ObjectInfo *obj;
	int file;
	int state;           /* typedef resolution: 0 new, 1 in progress, 2 done */
	SrcLoc loc;
} TypeName;

typedef struct BlockCtx {
	Block *block;
	int index;           /* statement being checked */
	struct BlockCtx *parent;
} BlockCtx;

/* A handler going into a parameter of `to`: where it came from, and
 * whether the call's receiver is part of the object making it. */
typedef struct HandlerFlow {
	SrcLoc loc;
	FnDecl *to;
	int to_param;
	FnDecl *from;          /* a handler parameter passed on: its function */
	int from_param;        /* ... and which parameter (-1: a method's name) */
	bool part_of_self;     /* the receiver is self, or a field of it */
	bool window_target;    /* a method's name: it belongs to a window */
	const char *name;      /* the method's name, for messages */
} HandlerFlow;

/* For workers (use threads): what each function calls, and the globals
 * that can change it uses, so a worker's `work` can be checked for using
 * one, anywhere it reaches */
typedef struct CallEdge {
	FnDecl *from, *to;
	bool on_self;          /* a method of self's: self's own class decides which */
} CallEdge;

typedef struct GlobalUse {
	FnDecl *fn;
	SrcLoc at;
	const char *name;
} GlobalUse;

typedef struct Checker {
	Arena *A;
	Sema *S;
	Program *prog;
	VEC(GlobalName) names;
	VEC(TypeName) types;
	int cur_file;

	/* per function */
	FnDecl *fn;
	ObjectInfo *self_obj;      /* class of the method being checked */
	Scope *scope;
	LoopInfo *loop;
	int pos;
	AccessVec acc;
	LoanVec loans;
	IntVec temps;
	Symbol *binding;           /* reference variable a borrow is stored into */
	int loops;                 /* enclosing loops */
	int breakables;            /* enclosing loops and switches */
	int unevaluated;           /* inside sizeof: no uses or loans recorded */
	BlockCtx *bctx;
	VEC(Stmt *) gotos;
	VEC(Stmt *) labels;
	VEC(SrcLoc) last_use;      /* per Symbol.index: location of the last use */

	Type *call_expected;       /* the type wanted from the call being checked (addresses.at) */

	/* handlers, checked once every body has been (see check_handler_flows) */
	VEC(HandlerFlow) flows;
	VEC(CallEdge) calls;
	VEC(GlobalUse) global_uses;
} Checker;

/* ---- diagnostics ---- */
void sema_error(SrcLoc l, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void sema_note(SrcLoc l, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void sema_warning(SrcLoc l, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
const char *ts(const Type *t);   /* type name for messages (ring of buffers) */

/* ---- names (sema_decl.c) ---- */
GlobalName *find_global(Checker *C, const char *name, SrcLoc use);
TypeName   *find_type(Checker *C, const char *name);
Type       *resolve_type(Checker *C, TypeExpr *te, unsigned flags, const char *where);
enum { RT_ALLOW_REF = 1, RT_ALLOW_VOID = 2, RT_ALLOW_SLICE = 4, RT_ALLOW_UNSIZED = 8, RT_ALLOW_HANDLER = 16, RT_ALLOW_CALLBACK = 32 };
FnDecl     *lookup_method(ObjectInfo *o, const char *name);
FieldInfo  *lookup_field(ObjectInfo *o, const char *name);
Builtin     find_builtin(const char *name);
void        check_handler_flows(Checker *C);
void        check_workers(Checker *C);
void        compute_object_layout(Checker *C, ObjectInfo *o);

/* ---- expressions (sema_expr.c) ---- */
Type *check_expr(Checker *C, Expr **ep, Ctx ctx, Type *expected);
bool  coerce(Checker *C, Expr **ep, Type *to, const char *what);
bool  bind_ref(Checker *C, Expr **ep, Type *ref_type, Symbol *binding, const char *what);
Type *check_cond(Checker *C, Expr **ep, const char *what);
bool  const_eval_int(Checker *C, Expr *e, int64_t *out);
bool  const_eval_float(Checker *C, Expr *e, double *out);
bool  is_constant_expr(Checker *C, Expr *e);
Type *int_literal_type(Expr *e, bool negated);
Expr *make_implicit_cast(Checker *C, Expr *e, Type *to);

/* ---- statements and functions (sema_stmt.c) ---- */
void check_function(Checker *C, FnDecl *fn);
Symbol *new_symbol(Checker *C, const char *name, SymbolKind kind, Type *type, bool is_const, SrcLoc loc);
void push_scope(Checker *C);
void pop_scope(Checker *C);
void declare(Checker *C, Symbol *s);
Symbol *lookup_local(Checker *C, const char *name);
void note_use(Checker *C, Symbol *s, SrcLoc loc);
void record_access(Checker *C, Symbol *s, AccessKind k, SrcLoc loc);
/* the same, for one part of s: fields, as place_path gives them */
void record_access_at(Checker *C, Symbol *s, const char *path, AccessKind k, SrcLoc loc);
/* Whether two parts of a variable overlap: one is the other, or inside it */
bool paths_overlap(const char *a, const char *b);
void add_loan(Checker *C, Symbol *root, const char *path, bool is_mut, SrcLoc loc, Symbol *borrower);
void add_source(Checker *C, Symbol *borrower, Symbol *src, SrcLoc loc);
void release_temps(Checker *C, int mark);

/* ---- ownership (sema_flow.c) ---- */
void flow_function(Checker *C, FnDecl *fn);
void check_loans(Checker *C, FnDecl *fn);

Symbol *address_of_local(const Expr *e);

Type *sema_peek_type(Checker *C, Expr *e);   /* the type e would have, checking a copy; NULL on errors */

void no_auto(VarDecl *v, Type *t);

#endif
