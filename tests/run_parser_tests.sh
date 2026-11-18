#!/usr/bin/env bash
# Parser regression tests. Usage: run_parser_tests.sh path/to/astdump
set -u
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
ASTDUMP=$(from_here "${1:-}" build/astdump)
needs "$ASTDUMP"
export CABBY_LIB=${CABBY_LIB:-$ROOT/lib}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0 fail=0

# st NAME STATEMENT EXPECTED — parses STATEMENT inside `int f { ... }`
st() {
	# types used by the statements are declared after the function (types are prescanned)
	printf 'int f {\n\t%s\n}\nobject window {\n}\nstruct point {\n\tint x\n}\n' "$2" > "$TMP/t.cby"
	local out; out=$("$ASTDUMP" "$TMP/t.cby" 2>/dev/null)
	local got; got=$(printf '%s\n' "$out" | sed -n '2p' | sed 's/^ *//')
	got=${got%)}   # the last statement line also closes `(fn ...`
	if [[ "$got" == "$3" ]]; then pass=$((pass+1)); else
		fail=$((fail+1)); echo "FAIL $1"; echo "  src:  $2"; echo "  want: $3"; echo "  got:  $got"; fi
}

# ast NAME SOURCE(printf format) EXPECTED-substring (output lines joined with ' / ')
ast() {
	printf "$2" > "$TMP/t.cby"
	local out; out=$("$ASTDUMP" "$TMP/t.cby" 2>&1); local rc=$?
	local got; got=$(printf '%s' "$out" | sed 's/^ *//' | tr '\n' '/' | sed 's|/| / |g')
	if [[ $rc -eq 0 && "$got" == *"$3"* ]]; then pass=$((pass+1)); else
		fail=$((fail+1)); echo "FAIL $1 (rc=$rc)"; echo "  want: $3"; echo "  got:  $got"; fi
}

# err NAME SOURCE(printf format) MESSAGE-substring EXPECTED-ERROR-COUNT
err() {
	printf "$2" > "$TMP/t.cby"
	local out; out=$("$ASTDUMP" "$TMP/t.cby" 2>&1); local rc=$?
	local n; n=$(printf '%s\n' "$out" | grep -c ': error: ')
	if [[ $rc -ne 0 && "$out" == *"$3"* && "$n" -eq "$4" ]]; then pass=$((pass+1)); else
		fail=$((fail+1)); echo "FAIL $1 (rc=$rc, errors=$n, want $4)"; echo "  want: $3"; echo "$out" | sed 's/^/  | /'; fi
}

# warn NAME SOURCE MESSAGE — parses cleanly but warns
warn() {
	printf "$2" > "$TMP/t.cby"
	local out; out=$("$ASTDUMP" "$TMP/t.cby" 2>&1 >/dev/null); local rc=$?
	if [[ $rc -eq 0 && "$out" == *"$3"* ]]; then pass=$((pass+1)); else
		fail=$((fail+1)); echo "FAIL $1 (rc=$rc)"; echo "  want warning: $3"; echo "$out" | sed 's/^/  | /'; fi
}

# --- C precedence & associativity -------------------------------------------------
st mul_over_add      'x = a + b * c'          '(= x (+ a (* b c)))'
st left_assoc        'x = a - b - c'          '(= x (- (- a b) c))'
st parens            'x = (a + b) * c'        '(= x (* (+ a b) c))'
st and_over_or       'x = a || b && c'        '(= x (|| a (&& b c)))'
st c_bitand_trap     'x = a & 1 == 0'         '(= x (& a (== 1 0)))'
st bit_order         'x = a | b ^ c & d'      '(= x (| a (^ b (& c d))))'
st shift_vs_add      'x = a << b + c'         '(= x (<< a (+ b c)))'
st eq_below_rel      'x = a < b == c < d'     '(= x (== (< a b) (< c d)))'
st not_vs_eq         'x = !a == b'            '(= x (== (! a) b))'
st neg_mul           'x = -a * b'             '(= x (* (- a) b))'
st bitnot            'x = ~a & b'             '(= x (& (~ a) b))'
st unary_plus        'x = +a'                 '(= x a)'
st cast              'x = (int)y * 2.0'       '(= x (* (cast int y) 2.0))'
st cast_chain        'x = (double)(int)y'     '(= x (cast double (cast int y)))'
st cast_unsigned     'x = (unsigned long)-1'  '(= x (cast unsigned long (- 1)))'
st paren_not_cast    'x = (y) - 1'            '(= x (- y 1))'
st sizeof_forms      'x = sizeof(int) + sizeof y + sizeof(long[4])' '(= x (+ (+ (sizeof int) (sizeof y)) (sizeof long[4])))'
st string_concat     'x = "ab" "cd"'          '(= x "abcd")'
st literals          "x = f(true, 'a', \"s\\n\", 0x10, 1e3, 2.5f, 7ul)" "(= x (call f true 'a' \"s\\n\" 16 1000.0 2.5f 7ul))"
st wrapped_binary    $'x = a +\n\t\tb'        '(= x (+ a b))'

# --- calls, bare calls --------------------------------------------------------------
st call              'f(a, b)'                '(call f a b)'
st bare_call         'f a, b'                 '(call! f a b)'
st bare_zero         'f'                      '(call! f)'
st bare_method       'w.resize 8, 6'          '(call! (. w resize) 8 6)'
st bare_method_zero  'w.show'                 '(call! (. w show))'
st bare_cast_arg     'print (int)x, y'        '(call! print (cast int x) y)'
st bare_negative     'print -1'               '(call! print (- 1))'
st bare_expr_arg     'print a + b, c'         '(call! print (+ a b) c)'
st bare_nested_call  'print add(1, 2)'        '(call! print (call add 1 2))'
st bare_init_arg     'draw point{1}'          '(call! draw {point: 1})'
st method_chain      'x = a.b(1).c[2]'        '(= x ([] (. (call (. a b) 1) c) 2))'
st self_field        'x = self.x.y'           '(= x (. (. self x) y))'
st static_call       'x = window.make(3)'     '(= x (call (. window make) 3))'
st trailing_comma    'f(a, b,)'               '(call f a b)'
st wrapped_args      $'f(a,\n\t\t\tb,\n  c)'  '(call f a b c)'

# --- simple statements ----------------------------------------------------------------
st assign            'x = y'                  '(= x y)'
st compound          'x <<= 2'                '(<<= x 2)'
st postfix_inc       'x++'                    '(+= x 1)'
st prefix_dec        '--x'                    '(-= x 1)'
st index_assign      'a[i] = 1'               '(= ([] a i) 1)'
st field_assign      'self.n = 0'             '(= (. self n) 0)'
st field_inc         'p.count++'              '(+= (. p count) 1)'
st assign_init       'p = {1, 2}'             '(= p {1 2})'
st return_void       'return'                 '(return)'
st return_val        'return a + 1'           '(return (+ a 1))'
st return_init       'return {1}'             '(return {1})'
st goto_            'goto done'               '(goto done)'
st label_           'done:'                   '(label done)'

# --- declarations ---------------------------------------------------------------------
st decl_plain        'int x'                  '(var int x)'
st decl_multi        'int a, b = 2, c[3]'     '(var int a) (var int b = 2) (var int[3] c)'
st decl_2d           'int g[2][3]'            '(var int[2][3] g)'
st decl_unsized      'int p[] = {2, 3}'       '(var int[] p = {2 3})'
st decl_const        'const int k = 4'        '(var const int k = 4)'
st decl_const_after  'int const k = 4'        '(var const int k = 4)'
st decl_ref          'const int &r = y'       '(var const int& r = y)'
st decl_mut_ref      'window &w = v'          '(var window& w = v)'
st decl_unsigned     'unsigned long n = 5ul'  '(var unsigned long n = 5ul)'
st decl_combo        'long long unsigned int n' '(var unsigned long n)'
st decl_char_kinds   'unsigned char u'        '(var unsigned char u)'
st decl_static       'static int calls'       '(var static int calls)'
st decl_register     'register int fast = 1'  '(var register int fast = 1)'
st decl_volatile     'volatile int flag'      '(var volatile int flag)'
st decl_auto         'auto z = 1.5'           '(var auto z = 1.5)'
st decl_str          'str s = "x"'            '(var str s = "x")'
st decl_object       'window w'               '(var window w)'
st decl_struct_kw    'struct window w'        '(var window w)'
st decl_designated   'window w = {.width = 5, 3}' '(var window w = {.width=5 3})'
st decl_typed_init   'auto w = window{1, 2}'  '(var auto w = {window: 1 2})'
st decl_nested_init  'int m[2][2] = {{1, 2}, {3, 4}}' '(var int[2][2] m = {{1 2} {3 4}})'
st decl_multiline_init $'window w = {\n\t\t.width = 5,\n\t\t.height = 3,\n\t}' '(var window w = {.width=5 .height=3})'

# --- compound statements -------------------------------------------------------------
ast if_chain   'int f {\n\tif a {\n\t\tx()\n\t} else if b {\n\t\ty()\n\t} else {\n\t\tz()\n\t}\n}\n' \
	'(if a / (call x) / else / (if b / (call y) / else / (call z)))'
ast else_next_line 'int f {\n\tif a {\n\t\tx()\n\t}\n\telse {\n\t\ty()\n\t}\n}\n' '(if a / (call x) / else / (call y))'
ast allman     'int f\n{\n\tif a\n\t{\n\t\tx()\n\t}\n}\n' '(fn int f () / (if a / (call x)))'
ast if_parens  'int f {\n\tif (a > 1) {\n\t\tx()\n\t}\n}\n' '(if (> a 1) / (call x))'
ast while_     'int f {\n\twhile x == y {\n\t\tbreak\n\t}\n}\n' '(while (== x y) / (break))'
ast do_while   'int f {\n\tdo {\n\t\tx--\n\t} while x > 0\n}\n' '(do-while (> x 0) / (-= x 1))'
ast for_c      'int f {\n\tfor int i = 0; i < n; i++ {\n\t\tcontinue\n\t}\n}\n' '(for ((var int i = 0)) ((< i n)) ((+= i 1)) / (continue))'
ast for_parens 'int f {\n\tfor (i = 0; i < n; i += 2) {\n\t}\n}\n' '(for ((= i 0)) ((< i n)) ((+= i 2)))'
ast for_ever   'int f {\n\tfor ;; {\n\t\tbreak\n\t}\n}\n' '(for () () () / (break))'
ast empty_block 'int f {\n\tif a { }\n}\n' '(if a)'
ast nested_block 'int f {\n\t{\n\t\tint x\n\t}\n}\n' '(block / (var int x))'
ast switch_    'int f {\n\tswitch n {\n\t\tcase 1:\n\t\t\tx()\n\t\t\tbreak\n\t\tcase 2:\n\t\tdefault:\n\t\t\ty()\n\t}\n}\n' \
	'(switch n / (case 1 / (call x) / (break)) / (case 2) / (default / (call y))))'
ast nested     'int f {\n\tif a {\n\t\tif b {\n\t\t\tx()\n\t\t}\n\t}\n\ty()\n}\n' '(if a / (if b / (call x))) / (call y)'

# --- declarations at top level ----------------------------------------------------
ast function_  'int add int a, int b {\n\treturn a + b\n}\n' '(fn int add (int a, int b) / (return (+ a b)))'
ast no_params  'void greet {\n\tprintln "hi"\n}\n' '(fn void greet () / (call! println "hi"))'
ast void_param 'int tick void {\n\treturn 1\n}\n' '(fn int tick ()'
ast prototype  'char give char c\nint tick void\n' '(proto char give (char c)) / (proto int tick ())'
ast ref_params 'object window {\n}\nvoid f const window &w, int &n, const int a[], str argv[] {\n}\n' \
	'(fn void f (const window& w, int& n, const int[] a, str[] argv))'
ast array_param 'void f int a[3] {\n}\n' '(fn void f (int[3]& a))'
ast extern_    'extern int printf str fmt, ...\nextern double sqrt double x\n' \
	'(extern fn int printf (str fmt, ...)) / (extern fn double sqrt (double x))'
ast static_fn  'static int helper {\n\treturn 1\n}\n' '(static fn int helper ()'
ast globals    'const int LIMIT = 10\nstatic int counter\nlong t[2] = {1, 2}\n' \
	'(global (var const int LIMIT = 10)) / (global static (var int counter)) / (global (var long[2] t = {1 2}))'
ast typedefs   'unsigned long aka size_t\ndouble aka vec3[3]\n' '(typedef size_t unsigned long) / (typedef vec3 double[3])'
ast enum_      'enum color {\n\tRED,\n\tGREEN = 5\n\tBLUE\n}\n' '(enum color RED GREEN=5 BLUE)'
ast enum_line  'enum e { A, B, C }\n' '(enum e A B C)'
ast struct_    'struct point {\n\tint x, y\n\tchar tag[4]\n}\n' '(struct point / (var int x) / (var int y) / (var char[4] tag))'
ast union_     'union bits {\n\tfloat f\n\tunsigned int u\n}\n' '(union bits / (var float f) / (var unsigned int u))'
ast object_    'object window : base, other {\n\tint width\n\tint area const {\n\t\treturn width\n\t}\n\tvoid resize int w {\n\t\twidth = w\n\t}\n\tstatic window make {\n\t\treturn window{}\n\t}\n\t~window {\n\t}\n}\n' \
	'(object window (parents base other) / (var int width) / (fn int area () const / (return width)) / (fn void resize (int w) / (= width w)) / (static fn window make () / (return {window:})) / (fn void ~window ())'
ast use_       'use standard\nint main {\n\treturn 0\n}\n' '(extern fn int printf'
ast cast_user_type 'struct p {\n\tint x\n}\nint f {\n\tx = (p)y\n}\n' '(= x (cast p y))'

# --- warnings ---------------------------------------------------------------------
warn bitand_cmp 'int f {\n\tif x & 1 == 0 {\n\t}\n}\n' "'&' binds more loosely than comparisons"

# --- errors: layout ----------------------------------------------------------------
err space_indent    'int f {\n    x = 1\n}\n'               'indentation must use tabs only' 1
err deep_indent     'int f {\n\t\tx = 1\n}\n'               'this line should be indented by 1 tab, not 2' 1
err shallow_body    'int f {\nx = 1\n}\n'                   'this line should be indented by 1 tab, not 0' 1
err closing_brace   'int f {\n\tif a {\n\t\tx = 1\n}\n}\n'  "the closing '}' should be indented by 1 tab, not 0" 1
err one_per_block   'int f {\n    a = 1\n    b = 2\n    c = 3\n}\n' 'indentation must use tabs only' 1
err missing_brace   'int f {\n\tif x\n\t\ty = 1\n}\n'       "expected '{' after the 'if' condition" 1
err missing_close   'int f {\n\tx = 1\n'                    "missing '}' to close the block" 1
err code_after_open 'int f { return 1\n}\n'                 "expected end of line after '{'" 1
err top_indent      '\tint f {\n\t}\n'                      'a top-level declaration should be indented by 0 tabs' 1

# --- errors: syntax --------------------------------------------------------------
err semicolon       'int f {\n\tx = 1;\n}\n'                "unexpected ';' after the statement" 1
err colon_after_if  'int f {\n\tif x: {\n\t}\n}\n'          "unexpected ':' after the 'if' condition" 1
err assign_in_cond  'int f {\n\tif x = 1 {\n\t}\n}\n'       "use '==' or 'is' to compare" 1
err elif            'int f {\n\tif a {\n\t} elif b {\n\t}\n}\n' "write 'else if', not 'elif'" 1
err stray_else      'int f {\n\telse {\n\t}\n}\n'           "'else' without a matching 'if'" 1
err case_outside    'int f {\n\tcase 1:\n}\n'               "'case' is only allowed directly inside a switch" 1
err two_defaults    'int f {\n\tswitch n {\n\t\tdefault:\n\t\tdefault:\n\t}\n}\n' "only have one 'default'" 1
err stmt_before_case 'int f {\n\tswitch n {\n\t\tx = 1\n\t}\n}\n' "must come after a 'case' or 'default' label" 1
err for_two_parts   'int f {\n\tfor i < 3 {\n\t}\n}\n'      "a 'for' header has three parts" 1
err do_no_while     'int f {\n\tdo {\n\t}\n}\n'             "expected 'while' or 'until' after the 'do' block" 1
err paren_params    'int f(int x) {\n}\n'                   'parameters are written without parentheses' 1
err pointer_decl    'int f {\n\tint *p\n}\n'                'Cabby has no pointers' 1
err address_of      'int f {\n\tx = &y\n}\n'                "there is no address-of '&'" 1
err deref           'int f {\n\tx = *p\n}\n'                "there is no '*' dereference" 1
err inc_in_expr     'int f {\n\tx = i++\n}\n'               "'++' can only be used as a statement" 1
err field_init      'object o {\n\tint x = 1\n}\n'          'fields cannot have initializers' 1
err struct_method   'struct s {\n\tint f {\n\t}\n}\n'       'a struct has no methods' 1
err const_method_pos 'object o {\n\tconst int f {\n\t}\n}\n' "put 'const' after its parameters" 1
err struct_inherits 'struct s : t {\n}\n'                   'only objects can inherit' 1
err bad_destructor  'object o {\n\t~other {\n\t}\n}\n'      "the destructor must be named '~o'" 1
err misspelled_type 'int f {\n\tstrng name = "x"\n}\n'      "unknown type 'strng'" 1
err type_hint       'void f string s {\n}\n'                "unknown type 'string'; did you mean 'str'?" 1
err top_statement   'x = 1\n'                               'statements must be inside a function' 1
err auto_no_init    'int f {\n\tauto x\n}\n'                "'auto' needs an initial value" 1
err const_no_init   'int f {\n\tconst int x\n}\n'           'a const variable needs an initial value' 1
err unsized_no_init 'int f {\n\tint a[]\n}\n'               'needs an initializer to give its length' 1
err variadic_plain  'int f int a, ... {\n}\n'               "variadic '...' is only allowed on extern" 1
err extern_body     'extern int f int a {\n}\n'             'cannot have a body' 1
err use_missing     'use local nope\n'                      "cannot find 'nope' (looked for" 1
err use_inside      'int f {\n\tuse x\n}\n'                 "'use' belongs at the top level" 1
err trailing_junk   'int f {\n\tx = 1 2\n}\n'               'expected end of line after the statement, found integer literal 2' 1

# --- errors: recovery reports each independent mistake once --------------------
err recover_two_fns 'int f {\n\tx = = 1\n\ty()\n}\nint g {\n\treturn )\n}\nint h {\n\treturn 0\n}\n' 'expected an expression' 2
err recover_blocks  'int f {\n\tif x: {\n\t\ta()\n\t} else {\n\t\tb()\n\t}\n\tz = = 2\n}\n' "unexpected ':'" 2
err recover_object  'object o {\n\tint x\n\tstrng y\n\tint m {\n\t\treturn +\n\t}\n\tbool z\n}\n' 'unknown type' 2
err recover_deep    'int f {\n\twhile a {\n\t\tif b {\n\t\t\tc = )\n\t\t}\n\t\td = )\n\t}\n\te = )\n}\n' 'expected an expression' 3

# for ... in
ast for_in_count    'int f {\n\tfor i in n {\n\t}\n}\n'                  '(for-in i n'
ast for_in_to       'int f {\n\tfor i in 1 to 10 by 2 {\n\t}\n}\n'        '(for-in i 1 to 10 by 2'
ast for_in_down     'int f {\n\tfor long i in n down to 0 {\n\t}\n}\n'    '(for-in long i n down-to 0'
ast for_in_expr     'int f {\n\tfor i in a + 1 to b * 2 {\n\t}\n}\n'      '(for-in i (+ a 1) to (* b 2)'
ast for_c_style     'int f {\n\tfor int i = 0; i < 3; i++ {\n\t}\n}\n'    '(for ('
err for_in_down_to  'int f {\n\tfor i in 5 down 1 {\n\t}\n}\n'            "expected 'to' after 'down'" 1
err for_in_name     'int f {\n\tfor in 5 {\n\t}\n}\n'                     "expected the counter's name" 1

# calls: at the start of a statement, the bracket belongs to the call only
# if it holds the whole rest of the line
st call_brackets    'print(a + b)'          '(call print (+ a b))'
st bare_paren_arg   'print (a + b) * 2'     '(call! print (* (+ a b) 2))'
st bare_paren_nospc 'print(a + b) * 2'      '(call! print (* (+ a b) 2))'
st bare_paren_list  'print (a), b'          '(call! print a b)'
st call_then_field  'w.get(0).x = 1'        '(= (. (call (. w get) 0) x) 1)'
st call_chain       'w.resize(1, 2)'        '(call (. w resize) 1 2)'
# brackets followed by more (even `.text`) start the first argument
st call_paren_chain 'print (n + 1).text + "x"' '(call! print (+ (. (+ n 1) text) "x"))'
st call_paren_dot   'print (n).text'         '(call! print (. n text))'
st call_paren_comma 'print(n).text, 5'       '(call! print (. n text) 5)'
st call_index_assign 'w.get(0)[1] = 1'       '(= ([] (call (. w get) 0) 1) 1)'
st call_chain_ends     'w.get(0).clear'         '(call! (. (call (. w get) 0) clear))'
st call_chain_bare_arg 'w.get(0).fill p'        '(call! (. (call (. w get) 0) fill) p)'
st call_space_is_bare  'print (n).text'         '(call! print (. n text))'
# (Type.method ...) calls a static method; it is not a cast
st static_not_cast  '(window.make 3).report' '(. (call! (. window make) 3) report)'
# inside expressions: name args... is a call; name(...) is an ordinary call
st expr_bare        'x = strlen s'           '(= x (call! strlen s))'
st expr_bare_paren  'x = (strlen s) + 5'     '(= x (+ (call! strlen s) 5))'
st expr_bare_args   'x = (add a, b) * 2'     '(= x (* (call! add a b) 2))'
st expr_c_call      'x = f(a) * 2'           '(= x (* (call f a) 2))'
st expr_minus       'x = n - 1'              '(= x (- n 1))'
st expr_bare_rest   'x = sq a + 3'           '(= x (call! sq (+ a 3)))'

echo "parser tests: $pass passed, $fail failed"
[[ $fail -eq 0 ]]
