/* ast.h — Cabby abstract syntax tree.
 *
 * The full language is described in README.md. In outline:
 *
 *   program   = { use | decl }
 *   use       = "use" [ "local" ] name NEWLINE      (name + ".hby")
 *   decl      = object_decl | struct_decl | union_decl | enum_decl
 *             | typedef_decl | function | prototype | extern_decl | globals
 *   function  = [ "static" ] type name params block
 *   params    = "void" | param { "," param }            (may be empty)
 *   param     = [ "const" ] type [ "&" ] name [ "[" [ N ] "]" ]
 *   object_decl = "object" name [ ":" parent { "," parent } ] "{" members "}"
 *   member    = field | method | "static" method | "~" name block
 *   method    = type name params [ "const" ] block
 *   block     = "{" NEWLINE { stmt } "}"     — lines indented one tab deeper
 *
 *   stmt      = decl_stmt | if | while | do | for | switch | return | break
 *             | continue | goto | label ":" | block | assignment | ++/--
 *             | call | bare_call
 *   bare_call = postfix_expr expr { "," expr }   (a call without parentheses,
 *                                                 only at the start of a line)
 *   for       = "for" [ init ] ";" [ cond ] ";" [ update ] block
 *             | "for" [ type ] name "in" range [ "by" expr ] block
 *   range     = expr | expr "to" expr | expr "down" "to" expr
 *
 * Expressions use C's operators and C's precedence, without ?: and without
 * unary & and * (references are declared, not taken; see README §9).
 *
 * The parser builds the tree; fields marked "sema" are filled by the checker
 * and read by the interpreter and the LLVM back end. Sema may rewrite nodes in
 * place: an identifier that names a field of the current object becomes
 * EXPR_FIELD(self, name), an enum constant becomes EXPR_INT, sizeof becomes
 * EXPR_INT, and implicit conversions and borrows are inserted as EXPR_CAST
 * (implicit) and EXPR_BORROW nodes.
 */
#ifndef AST_H
#define AST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "arena.h"

typedef struct SrcLoc {
	int file, line, col;
} SrcLoc;

/* ---- arena-backed growable arrays ------------------------------------- */

#define VEC(T) struct { T *items; int len; int cap; }

#define vec_push(A, v, x)                                                      \
	do {                                                                   \
		if ((v).len == (v).cap) {                                      \
			int nc_ = (v).cap ? (v).cap * 2 : 4;                   \
			void *ni_ = arena_alloc((A), (size_t)nc_ * sizeof *(v).items); \
			if ((v).len)                                           \
				memcpy(ni_, (v).items, (size_t)(v).len * sizeof *(v).items); \
			(v).items = ni_;                                       \
			(v).cap = nc_;                                         \
		}                                                              \
		(v).items[(v).len++] = (x);                                    \
	} while (0)

struct Type;
struct Symbol;

typedef struct TypeExpr   TypeExpr;
typedef struct Expr       Expr;
typedef struct Stmt       Stmt;
typedef struct Block      Block;
typedef struct FnDecl     FnDecl;
typedef struct ObjectDecl ObjectDecl;
typedef struct EnumDecl   EnumDecl;
typedef struct VarDecl    VarDecl;
typedef struct Decl       Decl;

typedef VEC(Expr *)          ExprVec;
typedef VEC(Stmt *)          StmtVec;
typedef VEC(Decl *)          DeclVec;
typedef VEC(FnDecl *)        FnDeclVec;
typedef VEC(VarDecl *)       VarDeclVec;
typedef VEC(struct Symbol *) SymbolVec;

/* ---- type expressions (as written) ------------------------------------ */

typedef enum {
	TYPEX_NAMED,   /* built-in (canonical spelling, e.g. "unsigned long") or a user type */
	TYPEX_REF,     /* T &name ; is_const for `const T &name` (shared borrow) */
	TYPEX_ARRAY,   /* T name[N] */
	TYPEX_SLICE,   /* T name[]  (parameters only: borrows an array of any length) */
	TYPEX_PRESET,  /* sema-made declarations: the type is already in `resolved` */
	TYPEX_LIST,    /* list T: a growable list; inner is T */
	TYPEX_LOOKUP,  /* lookup K V: values found by key; key is K, inner is V */
	TYPEX_ADDRESS, /* address T (use addresses): inner is T */
} TypeExprKind;

struct TypeExpr {
	TypeExprKind kind;
	SrcLoc loc;
	const char *name;      /* NAMED */
	bool builtin;          /* NAMED: a keyword type */
	TypeExpr *inner;       /* REF, ARRAY, SLICE, LIST; LOOKUP: the values */
	TypeExpr *key;         /* LOOKUP: the keys */
	Expr *len;             /* ARRAY: constant expression; NULL = from initializer */
	bool is_const;         /* REF/SLICE: shared rather than mutable */
	struct Type *resolved; /* sema */
};

/* ---- expressions ------------------------------------------------------ */

typedef enum {
	EXPR_INT,
	EXPR_FLOAT,
	EXPR_BOOL,
	EXPR_CHAR,
	EXPR_STRING,
	EXPR_IDENT,
	EXPR_SELF,
	EXPR_UNARY,      /* -x  !x  ~x  (+x is parsed away) */
	EXPR_BINARY,
	EXPR_ASSIGN,     /* x = e, x += e, x++ (as += 1) — statement position only */
	EXPR_CALL,
	EXPR_FIELD,      /* a.b */
	EXPR_INDEX,      /* a[i] */
	EXPR_CAST,       /* (T)e, or an implicit conversion inserted by sema */
	EXPR_INIT,       /* {a, b}, {.x = a}, T{...} */
	EXPR_SIZEOF,     /* sizeof(T) / sizeof e — rewritten to EXPR_INT by sema */
	EXPR_BORROW,
	EXPR_HANDLER,   /* sema: a window's method passed as a handler */
	EXPR_AT,        /* sema: what's at an address (A.value, A[i]): a place */     /* inserted by sema where a reference is bound */
} ExprKind;

typedef enum { UN_NEG, UN_NOT, UN_BITNOT } UnaryOp;

typedef enum {
	BIN_ADD, BIN_SUB, BIN_MUL, BIN_DIV, BIN_MOD,
	BIN_BIT_AND, BIN_BIT_OR, BIN_BIT_XOR, BIN_SHL, BIN_SHR,
	BIN_LOG_AND, BIN_LOG_OR,
	BIN_EQ, BIN_NE, BIN_LT, BIN_LE, BIN_GT, BIN_GE,
	BIN__COUNT
} BinaryOp;

typedef enum {
	USE_NONE = 0,
	USE_COPY,       /* Copy type: bitwise copy, source stays valid */
	USE_MOVE,       /* owned type: source becomes moved-from */
	USE_BORROW,     /* read in place without consuming */
} UseKind;

typedef enum {
	RECV_NONE = 0,
	RECV_AUTOREF,      /* owned receiver, method takes it by shared borrow */
	RECV_AUTOREF_MUT,  /* owned receiver, method takes it by mutable borrow */
	RECV_PASS_REF,     /* receiver is a reference variable: pass it on */
} RecvMode;

typedef struct InitEntry {
	const char *field;   /* designator (.name), or NULL for positional */
	Expr *key;           /* a lookup's `key: value`, or NULL */
	SrcLoc loc;
	Expr *value;
} InitEntry;

typedef VEC(InitEntry) InitVec;

struct Expr {
	ExprKind kind;
	SrcLoc loc;

	struct Type *ty;   /* sema */
	UseKind use;       /* sema */
	bool to_owned_str; /* sema: string literal becomes an owned heap copy */
	bool parenthesized;/* written inside ( ) — used for precedence warnings */
	bool generated;    /* made by sema (a for-in loop's own update): may change its read-only counter */
	void *backend;     /* scratch slot for the interpreter / code generator */

	union {
		struct {
			uint64_t value;       /* magnitude; sign comes from UN_NEG (or sema folding) */
			bool is_unsigned;     /* u suffix */
			uint8_t longs;        /* l / ll suffix */
			bool based;           /* hex/binary/octal: C lets these become unsigned */
			bool from_length;     /* an array's .length, folded to its value */
		} int_lit;
		struct { double value; bool is_float32; } float_lit;
		bool bool_val;
		uint8_t char_val;
		struct { const char *data; size_t len; } str;

		struct {
			const char *name;
			struct Symbol *sym;   /* sema */
		} ident;

		struct { UnaryOp op; Expr *operand; } unary;
		struct { BinaryOp op; Expr *lhs, *rhs; } binary;

		struct {
			bool compound;        /* +=, ++ ... */
			BinaryOp op;
			Expr *target;
			Expr *value;
		} assign;

		struct {
			Expr *callee;         /* IDENT (function) or FIELD (method) */
			ExprVec args;
			bool bare;            /* written without parentheses */
			bool minus;           /* `f -x`: sema turns it back into f - x if f takes no arguments */
			FnDecl *fn;           /* sema: static target */
			int builtin;          /* sema: Builtin id, 0 if none */
			RecvMode recv;        /* sema */
			bool is_static;       /* sema: type.method(...) — no receiver */
			uint8_t implicit;     /* sema: a name used alone, so called: 1 function, 2 method */
			bool is_virtual;      /* sema: dispatch through the vtable */
			int slot;             /* sema: vtable slot, -1 if none */
			unsigned checked_args; /* sema: arguments checked already, a bit each (a type's method: bit 0) */
			bool worker_check;     /* sema: a worker's method, from outside it: not while it works */
		} call;

		struct {
			Expr *object;
			const char *name;
			int field_index;      /* sema: index among the owner's own fields */
			int owner_id;         /* sema: declaring class/struct/union id */
			bool worker_check;    /* sema: a worker's field, from outside it: not while it works */
		} field;

		struct {
			Expr *base, *index;
			bool on_str;          /* sema: s[i] = c on a str */
			bool is_slice;        /* s[a to b]: index is a (NULL if left out), to is b */
			Expr *to;             /* NULL if left out: s[a to] */
			bool lookup_put;      /* sema: Lookup[k] = v, which adds k if it is new */
			uint8_t lookup_part;  /* sema: 1 the key, 2 the value at position index (for-in over a lookup) */
		} index;
		struct { Expr *value; TypeExpr *target; bool implicit; } cast;

		struct {
			TypeExpr *type;       /* T{...}; NULL for a bare {...} */
			InitVec entries;
			Expr **slots;         /* sema: one per field / element of the resolved type; NULL = zero */
			int nslots;
		} init;

		struct { TypeExpr *type; Expr *expr; } size_of;   /* exactly one set */

		struct { Expr *operand; bool is_mut; } borrow;
		struct { Expr *address; Expr *step; } at;   /* EXPR_AT: step may be NULL */

		struct {
			struct FnDecl *method;
			int slot;             /* vtable slot: the window's own override is called */
			struct Symbol *self;  /* the window */
		} handler;
	} as;
};

/* ---- variables -------------------------------------------------------- */

struct VarDecl {
	const char *name;
	SrcLoc loc;
	TypeExpr *type;          /* NULL with is_auto */
	Expr *init;              /* optional */
	bool is_const;           /* const binding (for references: shared borrow) */
	bool is_static;
	bool is_register;
	bool is_volatile;
	bool is_auto;
	bool is_extern_str;      /* extern parameter of type str: passed as a C string */
	bool is_inner;           /* field: only its object's own methods may use it */
	struct Symbol *sym;      /* sema */
};

/* ---- statements ------------------------------------------------------- */

typedef enum {
	STMT_DECL,
	STMT_EXPR,
	STMT_IF,
	STMT_WHILE,
	STMT_DO,
	STMT_FOR,
	STMT_SWITCH,
	STMT_RETURN,
	STMT_BREAK,
	STMT_CONTINUE,
	STMT_GOTO,
	STMT_LABEL,
	STMT_BLOCK,
} StmtKind;

/* for [T] name in end
 * for [T] name in start to end          (end included)
 * for [T] name in start down to end     (counting down; end included)
 * ... by step                           (step > 0) */
typedef struct ForRange {
	VarDecl *var;            /* the counter; var->type NULL: the range's type */
	VarDecl *var2;           /* `for K, V in Lookup`: the value; NULL otherwise */
	Expr *start;             /* NULL: 0 */
	Expr *end;
	Expr *step;              /* NULL: 1 */
	bool inclusive;          /* `to` */
	bool down;               /* `down to` */
} ForRange;

struct Block {
	SrcLoc loc;
	StmtVec stmts;
	/* sema: locals declared directly in this block, in declaration order;
	 * dropped in reverse order whenever the block is left. */
	SymbolVec locals;
	Block *parent;           /* sema */
};

typedef struct SwitchCase {
	SrcLoc loc;
	ExprVec values;          /* empty for `default:` */
	bool is_default;
	Block *body;             /* statements up to the next case label */
	int64_t *consts;         /* sema: folded values (one per entry in values) */
} SwitchCase;

typedef VEC(SwitchCase) CaseVec;

/* `... else V`, `... else { }` or `... else Why { }` after a declaration
 * or a statement: what's done if working it out panics. The panic is
 * caught (and not shown); a declaration's variable then gets `value`, or
 * `block` runs, with the panic's message in `why` (a str, declared at the
 * block's start). An assignment's `else V` is made a block that assigns V. */
typedef struct Recover {
	SrcLoc loc;
	Expr *value;
	Block *block;
	const char *why;
} Recover;

struct Stmt {
	StmtKind kind;
	SrcLoc loc;
	Recover *recover;        /* NULL, or what to do if it panics */
	union {
		VarDeclVec decl;

		Expr *expr;

		struct {
			Expr *cond;
			Block *then_block;
			Stmt *else_branch;       /* NULL, STMT_IF (else if) or STMT_BLOCK */
		} if_;

		struct { Expr *cond; Block *body; } while_;   /* also STMT_DO */

		struct {
			struct ForRange *range;  /* `for i in ...`; sema turns it into init/cond/update */
			Stmt *init;              /* STMT_DECL or STMT_EXPR, or NULL */
			Expr *cond;              /* NULL = forever */
			Stmt *update;            /* STMT_EXPR or NULL */
			Block *body;
			Block *scope;            /* sema: holds the init declaration's locals */
		} for_;

		struct {
			Expr *value;
			CaseVec cases;
			int default_index;       /* -1 if none */
		} switch_;

		struct { Expr *value; } ret;

		struct {
			const char *label;
			Stmt *target;            /* sema: the STMT_LABEL */
			Block *target_block;     /* sema */
			int target_index;        /* sema: index of the label in target_block */
		} goto_;

		struct {
			const char *name;
			Block *block;            /* sema */
			int index;               /* sema */
		} label;

		Block *block;
	} as;
};

/* ---- declarations ----------------------------------------------------- */

typedef enum {
	SELF_NONE,      /* free function or static method */
	SELF_REF,       /* const method: shared borrow of the receiver */
	SELF_MUT_REF,   /* ordinary method: mutable borrow */
} SelfKind;

struct FnDecl {
	const char *name;
	SrcLoc loc;
	SelfKind self_kind;
	VarDeclVec params;
	TypeExpr *ret;           /* never NULL (void is spelled out) */
	Block *body;             /* NULL for prototypes and externs */
	bool is_extern;
	bool is_variadic;        /* extern only: trailing `...` */
	bool is_static;          /* file-local function, or static method */
	bool is_destructor;      /* ~name */
	bool is_maker;           /* name { }: runs when an object is made */
	bool is_inner;           /* only its object's own methods may call it */
	bool is_prototype;       /* declared without a body */
	FnDecl *definition;      /* sema: for prototypes, the matching definition */

	ObjectDecl *owner;       /* enclosing object, NULL for free functions */
	FnDecl *overrides;       /* sema */
	int vtable_slot;         /* sema: -1 if not virtual */
	struct Type *ret_type;   /* sema */

	/* sema: every variable of the function — self, params, then locals in
	 * declaration order. Symbol.index is the position here. */
	SymbolVec frame;
	struct Symbol *self_sym; /* sema */
	unsigned handler_keeps;  /* sema: handler parameters it stores in a field (bit per parameter) */
	unsigned handler_native; /* sema: handler parameters it gives to C (a window's widgets) */
	void *backend;
	void *bytecode;          /* the interpreter's bytecode for it (interpreter_bytecode.c) */
	int bc_state;            /* 0 not tried yet, 1 made, 2 not made: the tree-walker runs it */
};

typedef enum { OBJ_OBJECT, OBJ_STRUCT, OBJ_UNION } ObjectKind;

typedef struct ParentRef {
	const char *name;
	SrcLoc loc;
	ObjectDecl *decl;        /* sema */
} ParentRef;

typedef VEC(ParentRef) ParentVec;

struct ObjectDecl {
	ObjectKind kind;
	const char *name;
	SrcLoc loc;
	ParentVec parents;       /* objects only; declaration order */
	VarDeclVec fields;       /* own fields */
	FnDeclVec methods;       /* own methods (objects only) */
	FnDecl *drop_fn;         /* the destructor, if declared */
	FnDecl *make_fn;         /* what runs when one is made (`rocket { }`), if declared */
	struct Type *ty;         /* sema */
};

typedef struct EnumItem {
	const char *name;
	SrcLoc loc;
	Expr *value;             /* optional */
	int64_t resolved;        /* sema */
} EnumItem;

typedef VEC(EnumItem) EnumItemVec;

struct EnumDecl {
	const char *name;
	SrcLoc loc;
	EnumItemVec items;
};

typedef enum {
	DECL_FN,
	DECL_OBJECT,             /* object, struct or union */
	DECL_ENUM,
	DECL_TYPEDEF,
	DECL_VAR,                /* global variable(s) */
} DeclKind;

struct Decl {
	DeclKind kind;
	SrcLoc loc;
	bool is_static;          /* file-local */
	union {
		FnDecl *fn;
		ObjectDecl *object;
		EnumDecl *enum_;
		struct { const char *name; TypeExpr *type; bool is_kind; bool is_strict; TypeExpr *steps; } typedef_;
		VarDeclVec vars;
	} as;
};

typedef struct SourceFile {
	const char *path;
	int id;                  /* diag file id; SrcLoc.file */
	char *src;
} SourceFile;

typedef VEC(SourceFile) SourceFileVec;

/* A library header (`use name`) and the directory it was found in; the
 * back ends look there for its native code (libcabby<name>.so / .a). */
typedef struct NativeLib {
	const char *name;
	const char *dir;
} NativeLib;

typedef VEC(NativeLib) NativeLibVec;

typedef struct Program {
	const char *main_file;
	NativeLibVec natives;
	DeclVec decls;           /* every file's declarations, dependencies first */
	SourceFileVec files;
	VEC(const char *) json_skipped; /* struct names, each followed by why json.from can't take it */
} Program;

/* ---- constructors & helpers (ast.c) ----------------------------------- */

TypeExpr   *ast_type_expr(Arena *a, TypeExprKind kind, SrcLoc loc);
TypeExpr   *ast_type_named(Arena *a, const char *name, bool builtin, SrcLoc loc);
Expr       *ast_expr(Arena *a, ExprKind kind, SrcLoc loc);
Expr       *ast_clone_expr(Arena *a, const Expr *e);
Stmt       *ast_stmt(Arena *a, StmtKind kind, SrcLoc loc);
Block      *ast_block(Arena *a, SrcLoc loc);
VarDecl    *ast_var(Arena *a, const char *name, SrcLoc loc);
FnDecl     *ast_fn(Arena *a, const char *name, SrcLoc loc);
ObjectDecl *ast_object(Arena *a, ObjectKind kind, const char *name, SrcLoc loc);
Decl       *ast_decl(Arena *a, DeclKind kind, SrcLoc loc);

const char *ast_binop_str(BinaryOp op);
const char *ast_unop_str(UnaryOp op);
bool        ast_binop_is_logical(BinaryOp op);
bool        ast_binop_is_bitwise(BinaryOp op);
bool        ast_binop_is_comparison(BinaryOp op);
bool        ast_binop_is_arith(BinaryOp op);
const char *ast_object_kind_name(ObjectKind k);

#endif
