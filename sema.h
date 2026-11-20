/* sema.h — Cabby semantic analysis: name resolution, C-style typing with
 * implicit conversions, object layout for multiple inheritance, and
 * Rust-style ownership checking.
 *
 * sema_check() works in stages:
 *
 *  1. Declarations (sema_decl.c). Collect every type, function, global and
 *     enum constant across all files; resolve typedefs; fold enum values;
 *     merge prototypes with definitions; enforce `static` file privacy.
 *  2. Objects. For each object compute the C3 method resolution order, the
 *     flattened field layout (a diamond's shared base appears once,
 *     base-most first), and a vtable indexed by method-name slot. Reject
 *     conflicting fields and methods, bad overrides, inheritance cycles, and
 *     types that contain themselves. Structs and unions are plain layouts.
 *  3. Bodies (sema_expr.c, sema_stmt.c). Type-check every function. Sema
 *     rewrites the tree so the back ends see explicit operations:
 *       - implicit numeric conversions become EXPR_CAST{implicit};
 *       - binding a reference becomes EXPR_BORROW;
 *       - a bare field/method name inside a method becomes self.name;
 *       - enum constants and sizeof become EXPR_INT;
 *       - a condition or && / || / ! operand becomes a bool (via a cast).
 *     Every use of a variable is classified as copy / move / borrow and
 *     recorded with a position, and every borrow creates a loan.
 *  4. Ownership (sema_flow.c). A flow-sensitive pass tracks which variables
 *     are moved or maybe-moved through if/else, loops, switch fall-through
 *     and forward goto; using a moved value is an error. Then each loan is
 *     live from its creation to the last use of the reference holding it
 *     (extended over a loop when used inside one the reference was declared
 *     outside of); a shared loan forbids writes, moves and mutable borrows of
 *     the borrowed variable while live, a mutable loan forbids every other
 *     access. References cannot be returned, stored in fields or arrays, so
 *     no lifetime annotations are needed.
 *
 * Known simplifications (all reject, never unsound):
 *  - Loans are per field, and per element at a constant position
 *    (place_path's "Shown.Id", "Scores[2]"); an element at a position
 *    worked out as it runs is all of its list or array, and a lookup's
 *    values are all of it.
 *  - Loan ranges are position intervals, not control-flow paths, so a
 *    borrow in one if-branch can conflict with a write in the other.
 *
 * Memory model consumed by the back ends
 * --------------------------------------
 *  - Integers are 8/16/32/64-bit, signed or unsigned, and wrap on overflow;
 *    float is IEEE single, double IEEE double. bool is 0/1.
 *  - str is an owned heap string; a string literal is a static str that is
 *    copied to the heap (Expr.to_owned_str) where an owned str is needed.
 *  - Objects are owned and heap-allocated; a value of object type always has
 *    exactly its static class, and polymorphism goes through references.
 *    Every object records its class, and a field's slot in an object of
 *    dynamic class K is K->block_start[owner_id] + field_index.
 *  - Structs and unions are plain values; a union's fields share storage.
 *  - Every variable starts at its type's zero value. Every owned local has
 *    a drop flag: set when it holds a value, cleared when moved from. At
 *    every block exit flagged locals are dropped in reverse declaration
 *    order. Dropping an object runs each class's destructor from the most
 *    derived to the base-most (reverse layout order), then drops its fields
 *    in layout order, then frees it.
 */
#ifndef SEMA_H
#define SEMA_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "arena.h"
#include "ast.h"

typedef enum {
	TY_ERROR,   /* poison: suppresses cascading errors */
	TY_VOID,
	TY_BOOL,
	TY_INT,     /* every integer type, including the char family and enums */
	TY_FLOAT,   /* float (32) and double (64) */
	TY_STR,
	TY_REF,     /* T & / const T & */
	TY_SLICE,   /* T [] parameter: borrow of an array of any length */
	TY_ARRAY,   /* T [len] */
	TY_LIST,    /* list T: growable, owns its elements; inner is T */
	TY_LOOKUP,  /* lookup K V: values found by key, in the order added; key is K, inner is V */
	TY_FILE,    /* file (use files): an open file; closed when dropped */
	TY_OBJECT,  /* object, struct or union (see obj->kind) */
	TY_HANDLER, /* a method of a gui.window, bound to it: `ok.on_click say_hello`.
		     * Parameters only; passed to C as two pointers (object, function). */
	TY_LINK,    /* link T: an object of T's, without owning it; inner is T. 8 bytes: a
	             * place in the runtime's table of linked objects, and that place's
	             * generation (rt.c's cby_link_*); 0 points nowhere */
	TY_ADDRESS, /* address T (use addresses): where a T is in memory; inner is T, or
		     * NULL for `address` of nothing in particular (C's void *) */
	TY_CALLBACK,/* `callback int comparer address void, address void`: a C function
		     * pointer, to a top-level function of that shape (sig) */
} TypeKind;

typedef struct ObjectInfo ObjectInfo;
typedef struct Type Type;

struct Type {
	TypeKind kind;
	uint8_t bits;      /* INT: 8/16/32/64; FLOAT: 32/64 */
	bool is_signed;    /* INT */
	bool is_char;      /* INT: printed as a character */
	bool is_mut;       /* REF / SLICE: mutable borrow */
	Type *inner;       /* REF, SLICE, ARRAY, LIST; LOOKUP: the values */
	Type *key;         /* LOOKUP: the keys */
	uint64_t len;      /* ARRAY */
	ObjectInfo *obj;   /* OBJECT */
	const char *name;  /* display name for enums / typedef'd ints (NULL: canonical) */
	const char *kind_name;   /* `long kind duration`: a kind of its own, not mixed with plain numbers */
	Type *base;        /* a kind: the number it is kept as */
	struct FnDecl *sig;/* CALLBACK: the shape of the functions it points to */
	Type *steps;       /* a kind that steps by another (a moment, by durations), or NULL */
	bool kind_strict;  /* `strict long kind duration`: plain numbers are never taken as one */
	const char *methods;     /* kinds and enums: functions named "<methods>.<name>" are its methods */
};

typedef struct FieldInfo {
	const char *name;
	Type *type;
	ObjectInfo *owner;   /* class that declares it */
	int own_index;       /* index among owner's own fields */
	int slot;            /* index in this class's flattened layout */
	uint64_t offset;     /* byte offset (structs and unions; used by sizeof and unions) */
	SrcLoc loc;
	bool is_inner;       /* only its object's own methods may use it */
} FieldInfo;

struct ObjectInfo {
	ObjectDecl *decl;
	ObjectKind kind;
	const char *name;
	int id;                    /* index in Sema.objects */
	int file;                  /* declaring file */
	Type *type;

	ObjectInfo **mro;          /* C3 linearization; mro[0] == this */
	int mro_len;
	ObjectInfo **layout;       /* field-block order: base-most first, each class once */
	int layout_len;
	int *block_start;          /* [Sema.nobjects]: first slot of class k's own fields, or -1 */
	FieldInfo *fields;         /* flattened, in slot order */
	int field_count;
	FnDecl **vtable;           /* [Sema.nslots]: implementation per method name, or NULL */
	bool has_user_drop;        /* some class in the layout has a destructor */
	bool has_maker;            /* some class in the layout runs something when made */
	bool has_derived;          /* some other object is made from it: a value of it may be one of those */
	uint64_t size, align;      /* structs/unions: C layout; objects: handle size */
	bool layout_done;

	int visit;                 /* internal */
};

typedef enum { SYM_LOCAL, SYM_PARAM, SYM_SELF, SYM_GLOBAL } SymbolKind;

typedef struct Symbol Symbol;

struct Scope;
struct LoopInfo;

struct Symbol {
	const char *name;
	SymbolKind kind;
	Type *type;
	bool is_const;             /* immutable binding */
	bool is_register;
	bool is_volatile;
	bool is_static_local;      /* SYM_GLOBAL declared inside a function */
	bool is_loop_var;          /* the counter of a for-in loop (read-only to the program) */
	SrcLoc loc;
	int index;                 /* LOCAL/PARAM/SELF: FnDecl.frame index; GLOBAL: Sema.globals index */
	VarDecl *decl;             /* globals: the declaration (initializer) */
	int file;                  /* globals: declaring file */
	bool file_static;          /* globals: `static` at top level */
	bool has_const_value;      /* const integer globals: folded value */
	int64_t const_value;

	/* internal to sema */
	struct Scope *scope;
	int decl_pos;
	int max_use;
	struct LoopInfo *ext_loop;
	int live_end;
	SymbolVec sources;
};

typedef enum {
	BUILTIN_NONE = 0,
	BUILTIN_PRINT,    /* print a, b, ...   — space-separated, no newline */
	BUILTIN_PRINTLN,  /* println a, b, ... — space-separated, newline   */
	/* inserted by sema; args are already checked */
	BUILTIN_STR_LEN,     /* s.length       (s)      -> long */
	BUILTIN_SLICE_LEN,   /* xs.length      (xs)     -> long, for a T xs[] parameter */
	BUILTIN_STR_COPY,    /* s.copy         (s)      -> str  */
	BUILTIN_STR_CONCAT,  /* a + b          (a, b)   -> str; each a str or a char */
	BUILTIN_STR_GET,     /* s[i]           (s, i)   -> char */
	BUILTIN_TEXT,        /* n.text         (n)      -> str: a number, bool or char as print shows it */
	BUILTIN_READ_LINE,   /* read_line      ()       -> str: a line from stdin, without its Enter */
	BUILTIN_INPUT,       /* input prompt   (p)      -> str: prints p, then read_line */
	BUILTIN_END_OF_INPUT,/* end_of_input   ()       -> bool: a read found nothing left */
	BUILTIN_READ_CHAR,   /* read_char      ()       -> char: a line's first character, the rest dropped */
	BUILTIN_READ_KEY,    /* read_key       ()       -> str: one key, without Enter ("a", "up", "enter") */
	BUILTIN_RECOVERED,   /* (an else block's Why) ()  -> str: the message of the panic it caught */
	BUILTIN_ABS,         /* abs x          (x)      -> x's type, without its sign */
	BUILTIN_CLOSE_TO,    /* close_to a, b[, limit]  -> bool: the same but for rounding */
	BUILTIN_RANDOM,      /* random a to b  (a, b)   -> a whole number from a to b, or a double from a up to b */
	BUILTIN_RANDOM_SEED, /* random_seed n  (n)      -> void: random's numbers from then on, the same each time */
	BUILTIN_EXIT,        /* exit status    (s)      -> void: the program ends, its files written out */
	BUILTIN_TEXT_DECIMALS,/* n.text places (n, p)   -> str: with that many decimal places ("4.50") */
	BUILTIN_STR_IS_INT,  /* s.is_int       (s)      -> bool */
	BUILTIN_STR_IS_LONG, /* s.is_long      (s)      -> bool */
	BUILTIN_STR_IS_NUMBER,/* s.is_number   (s)      -> bool */
	BUILTIN_STR_TO_INT,  /* s.to_int       (s)      -> int, panics if s isn't one */
	BUILTIN_STR_TO_LONG, /* s.to_long      (s)      -> long */
	BUILTIN_STR_TO_DOUBLE,/* s.to_double   (s)      -> double */
	BUILTIN_STR_FIND,    /* s.find t       (s, t)   -> long: where t starts, or -1 */
	BUILTIN_STR_CONTAINS,/* s.contains t   (s, t)   -> bool */
	BUILTIN_STR_STARTS_WITH, /* s.starts_with t      -> bool */
	BUILTIN_STR_ENDS_WITH,   /* s.ends_with t        -> bool */
	BUILTIN_STR_TRIM,    /* s.trim         (s)      -> str */
	BUILTIN_STR_UPPER,   /* s.upper        (s)      -> str */
	BUILTIN_STR_LOWER,   /* s.lower        (s)      -> str */
	BUILTIN_STR_REPLACE, /* s.replace a, b (s, a, b) -> str */
	BUILTIN_STR_REPEAT,  /* s.repeat n     (s, n)   -> str */
	BUILTIN_STR_PAD_LEFT,  /* s.pad_left n   (s, n) -> str: spaces in front, to n characters */
	BUILTIN_STR_PAD_RIGHT, /* s.pad_right n  (s, n) -> str: spaces after */
	BUILTIN_STR_CENTERED,  /* s.centered n   (s, n) -> str: spaces either side */
	BUILTIN_STR_COMPARE, /* a < b etc.     (a, b)   -> int: below 0, 0, above 0 */
	BUILTIN_STR_SLICE,   /* s[a to b]      (s, a, b, flags) -> str; flags RT_SLICE_NO_FROM/NO_TO */
	BUILTIN_LIST_LEN,    /* L.length       (L)      -> long */
	BUILTIN_LIST_ADD,    /* L.add x        (L, x)   x moved in */
	BUILTIN_LIST_INSERT, /* L.insert i, x  (L, i, x) */
	BUILTIN_LIST_REMOVE_AT, /* L.remove_at i (L, i)  the element is dropped */
	BUILTIN_LIST_TAKE_LAST, /* L.take_last  (L)      -> the last element, moved out */
	BUILTIN_LIST_CLEAR,  /* L.clear        (L) */
	BUILTIN_LIST_CONTAINS, /* L.contains x (L, x)   -> bool */
	BUILTIN_LIST_FIND,   /* L.find x       (L, x)   -> long, or -1 */
	BUILTIN_LIST_COPY,   /* L.copy         (L)      -> a deep copy */
	BUILTIN_LIST_SLICE,  /* L[a to b]      (L, a, b, flags) -> a new list of copies */
	BUILTIN_LIST_JOIN,   /* L.join sep     (L, sep) -> str, for a list str */
	BUILTIN_LIST_SORT,   /* L.sort         (L)      smallest first; strings in dictionary order */
	BUILTIN_LIST_SORT_BY,   /* L.sort_by F    (L, CALLBACK) by a field, or a function's key; stable */
	BUILTIN_LIST_SORT_WITH, /* L.sort_with F  (L, CALLBACK) F(A, B) true: A goes first; stable */
	BUILTIN_LIST_REVERSE, /* L.reverse     (L)      last element first */
	BUILTIN_LOOKUP_LEN,  /* K.length       (K)      -> long */
	BUILTIN_LOOKUP_CONTAINS, /* K.contains k (K, k) -> bool */
	BUILTIN_LOOKUP_REMOVE, /* K.remove k   (K, k)   the key and its value are dropped; panics if missing */
	BUILTIN_LOOKUP_CLEAR, /* K.clear       (K) */
	BUILTIN_LOOKUP_COPY, /* K.copy         (K)      -> a deep copy */
	BUILTIN_LOOKUP_KEYS, /* K.keys         (K)      -> list of copies of the keys, in order */
	BUILTIN_LOOKUP_VALUES, /* K.values     (K)      -> list of copies of the values, in order */
	BUILTIN_LOOKUP_NEXT, /* for-in over K   (K, pos) -> the next position in use at or after pos, or -1 */
	/* use files */
	BUILTIN_FILES_READ,   /* files.read Path         -> str, the whole file */
	BUILTIN_FILES_LINES,  /* files.lines Path        -> list str, without their line ends */
	BUILTIN_FILES_WRITE,  /* files.write Path, Text  the file becomes Text */
	BUILTIN_FILES_APPEND, /* files.append Path, Text Text goes on the end */
	BUILTIN_FILES_EXISTS, /* files.exists Path       -> bool: a file (not a directory) is there */
	BUILTIN_FILES_SIZE,   /* files.size Path         -> long, in bytes */
	BUILTIN_FILES_COPY,   /* files.copy From, To */
	BUILTIN_FILES_RENAME, /* files.rename From, To   (also moves it) */
	BUILTIN_FILES_DELETE, /* files.delete Path */
	/* use directories */
	BUILTIN_DIRS_LIST,    /* directories.list Path      -> list str: the names in it, sorted */
	BUILTIN_DIRS_ALL_FILES, /* directories.all_files Path -> list str: every file below, as paths from Path */
	BUILTIN_DIRS_EXISTS,  /* directories.exists Path */
	BUILTIN_DIRS_MAKE,    /* directories.make Path      (and any missing above it) */
	BUILTIN_DIRS_REMOVE,  /* directories.remove Path    (it must be empty) */
	BUILTIN_DIRS_RENAME,  /* directories.rename From, To */
	BUILTIN_DIRS_CURRENT, /* directories.current        -> str */
	BUILTIN_DIRS_CHANGE_TO, /* directories.change_to Path */
	BUILTIN_DIRS_HOME,    /* directories.home           -> str */
	BUILTIN_DIRS_SAVE_FOLDER, /* directories.save_folder Name -> str (made, if it isn't there) */
	BUILTIN_FILES_OPEN,   /* files.open Path         -> file, for reading */
	BUILTIN_FILES_OPEN_TO_WRITE, /* files.open_to_write Path -> file: replaces what was there */
	BUILTIN_FILES_OPEN_TO_ADD,   /* files.open_to_add Path   -> file: adds to the end */
	BUILTIN_FILES_OPEN_TO_CHANGE, /* files.open_to_change Path -> file: read and write anywhere in it */
	/* use shell */
	BUILTIN_SHELL_RUN,    /* shell.run Command       -> str: what it printed (panics if it fails) */
	BUILTIN_SHELL_STATUS, /* shell.status Command    -> int: its exit status; its output shows */
	BUILTIN_SHELL_GET,    /* shell.get Name          -> str: an environment variable (panics if unset) */
	BUILTIN_SHELL_HAS,    /* shell.has Name          -> bool */
	BUILTIN_SHELL_SET,    /* shell.set Name, Value */
	BUILTIN_SHELL_UNSET,  /* shell.unset Name */
	BUILTIN_FILE_READ_LINE, /* F.read_line           -> str, the next line without its end */
	BUILTIN_FILE_AT_END,  /* F.at_end                -> bool: nothing more to read */
	BUILTIN_FILE_HANDLE,  /* F.handle (library headers only) -> long: the runtime's file, for C */
	BUILTIN_TO_FILE,      /* F call  (F, call)       -> the call's value; its printing goes into F */
	BUILTIN_STR_SPLIT,   /* s.split sep    (s, sep) -> list str */
	BUILTIN_TO_STDOUT,   /* stdout call    (call)   -> the call's value; its printing goes to stdout */
	BUILTIN_TO_STDERR,   /* stderr call    (call)   -> the same, to stderr */
	BUILTIN_HANDLER_CALL,   /* H.call        (H)       calls it (nothing if it isn't set) */
	BUILTIN_HANDLER_IS_SET, /* H.is_set      (H)       -> bool */
	BUILTIN_HANDLER_CLEAR,  /* H.clear       (H)       not set any more */
	BUILTIN_HANDLER_SET,    /* Field = h     (field, h) a handler parameter stored in a field */
	/* use addresses */
	BUILTIN_ADDRESSES_OF,   /* addresses.of X    -> address T: where X is (an array: its first element) */
	BUILTIN_ADDRESSES_AT,   /* addresses.at N    -> address T: the address N (T from the variable) */
	BUILTIN_ADDRESSES_NONE, /* addresses.none    -> address T: no address (C's NULL) */
	BUILTIN_ADDRESSES_ALLOCATE,   /* addresses.allocate N    -> address T: room for N values, zeroed */
	BUILTIN_ADDRESSES_FREE,       /* addresses.free A        gives the memory back */
	BUILTIN_ADDRESSES_REALLOCATE, /* addresses.reallocate A, N -> address T: the block grown or shrunk */
	BUILTIN_ADDRESSES_OF_TEXT,    /* addresses.of_text S     -> address char: its characters, then '\0' */
	BUILTIN_ADDRESSES_OF_ITEMS,   /* addresses.of_items L    -> address T: a list's first item */
	BUILTIN_ADDRESS_NUMBER, /* A.number          -> long: the address as a number */
	BUILTIN_ADDRESS_IS_NONE,/* A.is_none         -> bool */
	BUILTIN_ADDRESS_DIFF,   /* A - B             -> long: how many values apart */
	BUILTIN_LINK_IS_SET,    /* L.is_set          -> bool: it points at an object that's still there */
	BUILTIN_LINK_CLEAR,     /* L.clear           -> void: it points nowhere */
	/* use events */
	BUILTIN_EVENTS_RUN,     /* events.run        waits, calling handlers, until nothing is left */
	BUILTIN_EVENTS_STOP,    /* events.stop       ends events.run early */
} Builtin;

typedef struct Sema {
	Program *prog;
	ObjectInfo **objects;
	int nobjects;
	const char **slot_names;   /* method name for each vtable slot */
	int nslots;
	FnDecl *main_fn;
	SymbolVec globals;         /* globals and static locals, in initialization order */
	FnDeclVec functions;       /* every function with a body (free functions and methods) */
} Sema;

bool sema_check(Arena *arena, Program *prog, Sema *out);

/* ---- type helpers (types.c) ---- */
extern Type t_error, t_void, t_bool, t_char, t_schar, t_uchar, t_short, t_ushort,
	t_int, t_uint, t_long, t_ulong, t_float, t_double, t_str, t_handler, t_file;

Type *type_ref(Arena *a, Type *inner, bool is_mut);
Type *type_slice(Arena *a, Type *inner, bool is_mut);
Type *type_array(Arena *a, Type *inner, uint64_t len);
Type *type_list(Arena *a, Type *inner);
Type *type_address(Arena *a, Type *inner);
Type *type_link(Arena *a, Type *inner);
Type *type_lookup(Arena *a, Type *key, Type *value);
Type *type_int_named(Arena *a, const Type *base, const char *name);
bool  type_eq(const Type *a, const Type *b);
bool  type_is_error(const Type *t);
bool  type_is_int(const Type *t);       /* integer, char, enum (not bool) */
bool  type_is_arith(const Type *t);     /* bool, integers, floats */
bool  type_is_plain(const Type *t);     /* no str, objects or borrows inside */
bool  type_is_copy(const Type *t);
bool  type_needs_drop(const Type *t);
bool  type_contains_ref(const Type *t);
Type *type_deref(Type *t);              /* strip one reference, if any */
bool  object_is_subclass(const ObjectInfo *derived, const ObjectInfo *base);
int   type_int_rank(const Type *t);
Type *type_promote(Type *t);            /* C integer promotion */
Type *type_common(Type *a, Type *b);    /* C usual arithmetic conversions */
bool  int_fits(const Type *t, int64_t v, bool v_unsigned);
uint64_t type_size(const Type *t);
uint64_t type_align(const Type *t);
const char *type_str(const Type *t, char *buf, size_t size);

#endif
