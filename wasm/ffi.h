/* wasm/ffi.h — what the interpreter uses of libffi, for WebAssembly, where
 * there's no libffi: a call goes to a wrapper made for that C function
 * (wasm/calls.c, made by tools/make-wasm-calls.py from the libraries'
 * extern declarations), which takes the arguments as the interpreter lays
 * them out and calls the function with its own C types. WebAssembly checks
 * a call's types exactly, so a function is never called through a pointer
 * of another shape. */
#ifndef CABBY_WASM_FFI_H
#define CABBY_WASM_FFI_H
#include <stdint.h>
#include <stddef.h>

typedef struct { size_t size; int kind; } ffi_type;
typedef enum { FFI_OK = 0, FFI_BAD_TYPEDEF, FFI_BAD_ABI } ffi_status;
typedef enum { FFI_DEFAULT_ABI = 0 } ffi_abi;
typedef uint64_t ffi_arg;
typedef int64_t ffi_sarg;
typedef struct { int nargs; } ffi_cif;
typedef struct { void *unused; } ffi_closure;
typedef void (*cby_wasm_call)(void **args, void *ret);
#define FFI_FN(f) ((void (*)(void))(f))

static ffi_type ffi_type_void = { 0, 0 }, ffi_type_uint8 = { 1, 1 }, ffi_type_sint8 = { 1, 2 },
		ffi_type_uint16 = { 2, 3 }, ffi_type_sint16 = { 2, 4 }, ffi_type_uint32 = { 4, 5 },
		ffi_type_sint32 = { 4, 6 }, ffi_type_uint64 = { 8, 7 }, ffi_type_sint64 = { 8, 8 },
		ffi_type_float = { 4, 9 }, ffi_type_double = { 8, 10 }, ffi_type_pointer = { 4, 11 };

static inline ffi_status ffi_prep_cif(ffi_cif *c, ffi_abi abi, unsigned n, ffi_type *r, ffi_type **a)
{
	(void)abi; (void)r; (void)a;
	c->nargs = (int)n;
	return FFI_OK;
}

/* (a C function that takes any number of arguments: none here) */
static inline ffi_status ffi_prep_cif_var(ffi_cif *c, ffi_abi abi, unsigned f, unsigned n, ffi_type *r, ffi_type **a)
{
	(void)c; (void)abi; (void)f; (void)n; (void)r; (void)a;
	return FFI_BAD_TYPEDEF;
}

static inline void ffi_call(ffi_cif *c, void (*fn)(void), void *ret, void **args)
{
	(void)c;
	((cby_wasm_call)(void *)fn)(args, ret);
}

/* callbacks (a Cabby function for C to call): not here, yet */
static inline void *ffi_closure_alloc(size_t size, void **code) { (void)size; *code = NULL; return NULL; }
static inline ffi_status ffi_prep_closure_loc(ffi_closure *c, ffi_cif *cif, void (*f)(ffi_cif *, void *, void **, void *),
					      void *data, void *code)
{
	(void)c; (void)cif; (void)f; (void)data; (void)code;
	return FFI_BAD_TYPEDEF;
}

/* the wrapper for the C function of this name, or NULL */
cby_wasm_call cby_wasm_find(const char *name);
#endif
