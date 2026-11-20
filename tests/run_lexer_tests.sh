#!/usr/bin/env bash
# Lexer regression tests. Usage: run_lexer_tests.sh path/to/lexdump
set -u
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
LEXDUMP=$(from_here "${1:-}" build/lexdump)
needs "$LEXDUMP"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0 fail=0

# kinds FILE -> token kinds (and values) joined with '|' (layout fields dropped)
kinds() { "$LEXDUMP" "$1" 2>/dev/null | awk '{ $1=""; $2=""; sub(/^ +/, ""); print }' | tr '\n' '|' | tr -s ' '; }
# layout FILE -> "kind@indent" for tokens that start a line
layout() { "$LEXDUMP" "$1" 2>/dev/null | awk '$2 ~ /^\^/ { sub(/^\^/, "", $2); k=$3; print k "@" $2 }' | tr '\n' ' '; }

expect_tokens() {
	printf "$2" > "$TMP/t.cby"
	local got; got=$(kinds "$TMP/t.cby")
	if [[ "$got" == *"$3"* ]]; then pass=$((pass+1)); else
		fail=$((fail+1)); echo "FAIL $1"; echo "  want: $3"; echo "  got:  $got"; fi
}

expect_layout() {
	printf "$2" > "$TMP/t.cby"
	local got; got=$(layout "$TMP/t.cby")
	if [[ "$got" == "$3" ]]; then pass=$((pass+1)); else
		fail=$((fail+1)); echo "FAIL $1"; echo "  want: $3"; echo "  got:  $got"; fi
}

expect_error() {
	printf "$2" > "$TMP/t.cby"
	local err; err=$("$LEXDUMP" "$TMP/t.cby" 2>&1 >/dev/null); local rc=$?
	if [[ $rc -ne 0 && "$err" == *"$3"* ]]; then pass=$((pass+1)); else
		fail=$((fail+1)); echo "FAIL $1 (rc=$rc)"; echo "  want error: $3"; echo "  got: $err"; fi
}

expect_ok() {
	printf "%%s" "$2" > "$TMP/t.cby"
	local err; err=$("$LEXDUMP" "$TMP/t.cby" 2>&1 >/dev/null); local rc=$?
	if [[ $rc -eq 0 && -z "$err" ]]; then pass=$((pass+1)); else
		fail=$((fail+1)); echo "FAIL $1"; echo "  $err"; fi
}

# --- layout -------------------------------------------------------------------
expect_tokens newline_ends   'a = 1\nb()\n' 'identifier a|=|integer literal 1|newline|identifier b|(|)|newline|end of file|'
expect_tokens final_newline  'a' 'identifier a|newline|end of file|'
expect_tokens blank_lines    'a\n\n\t\n   \n// c\n\t// indented\nb\n' 'identifier a|newline|identifier b|newline|end of file|'
expect_tokens trailing_comment 'a // c\nb\n' 'identifier a|newline|identifier b|newline|'
expect_tokens block_comment  'a /* x */ b\n/* one\ntwo */\nc\n' 'identifier a|identifier b|newline|identifier c|newline|'
expect_tokens paren_join     'f(a,\n      b,\nc)\nd\n' 'identifier a|,|identifier b|,|identifier c|)|newline|identifier d|'
expect_tokens braces_dont_join 'if x {\n\ty\n}\n' 'if|identifier x|{|newline|identifier y|newline|}|newline|'
expect_tokens crlf           'a\r\n\tb\r\n' 'identifier a|newline|identifier b|newline|end of file|'
expect_tokens empty_file     '' 'end of file|'
expect_layout indents        'a {\n\tb {\n\t\tc\n\t}\n}\n' 'identifier@0 identifier@1 identifier@2 }@1 }@0 end@0 '
expect_layout comment_lines  'a\n\t// x\n\t\tb\n' 'identifier@0 identifier@2 end@0 '
expect_error  unterminated_comment 'a /* never\n' 'unterminated /* comment'

# --- directives -----------------------------------------------------------------
expect_tokens use_lib        'use standard\n' 'use declaration standard.hby|newline|'
expect_tokens use_local      'use local util // note\n' 'use declaration local util.hby|newline|'
expect_tokens use_subdir     'use local geo/shapes\n' 'use declaration local geo/shapes.hby|newline|'
expect_error  use_hash       '#use standard.hby\n' "write 'use name' without the '#'"
expect_error  use_extension  'use standard.hby\n' "leave out '.hby': write 'use standard'"
expect_error  use_two        'use a b\n' "put each on its own line"
expect_error  bad_directive  '#include <stdio.h>\n' "Cabby has no '#' directives"
expect_tokens range_keywords 'x in to down by\n' 'identifier x|in|to|down|by|newline|'
expect_tokens use_mid_line   'x use\n' 'identifier x|use|newline|'
expect_error  hash_midline   'a # b\n' "Cabby has no '#' directives"

# --- operators ------------------------------------------------------------------
expect_tokens logical_vs_bitwise 'a && b & c || d | e ^ ~f\n' \
	'identifier a|&&|identifier b|&|identifier c||||identifier d|||identifier e|^|~|identifier f|'
expect_tokens incdec         'i++ --j\n' 'identifier i|++|--|identifier j|'
expect_tokens compound_ops   'a <<= b >>= c &= d |= e ^= f %%= g\n' '<<=|identifier b|>>=|identifier c|&=|identifier d||=|identifier e|^=|identifier f|%=|'
expect_tokens comparisons    'a == b != c <= d >= e < f > g\n' '==|identifier b|!=|identifier c|<=|identifier d|>=|identifier e|<|identifier f|>|'
expect_tokens for_header     'for int i = 0; i < n; i++ {\n' 'for|int|identifier i|=|integer literal 0|;|identifier i|<|identifier n|;|identifier i|++|{|'
expect_tokens ellipsis       'f int a, ...\n' 'identifier f|int|identifier a|,|...|'
expect_tokens destructor     '~window {\n' '~|identifier window|{|'
expect_tokens case_colon     'case 1:\n' 'case|integer literal 1|:|'
expect_error  arrow          'a->b\n' "there is no '->' in Cabby"
expect_error  ternary        'a ? b : c\n' "there is no '?:' operator"

# --- keywords -------------------------------------------------------------------
expect_tokens c_keywords 'auto break case char const continue default do double else enum extern float for goto if int long register return short signed sizeof static struct switch typedef union unsigned void volatile while aka is not and or until inner callback\n' \
	'auto|break|case|char|const|continue|default|do|double|else|enum|extern|float|for|goto|if|int|long|register|return|short|signed|sizeof|static|struct|switch|typedef|union|unsigned|void|volatile|while|aka|is|not|and|or|until|inner|callback|newline|'
expect_tokens extra_keywords 'str object bool true false self\n' 'str|object|bool|true|false|self|newline|'
expect_tokens not_keywords   'fn let mut elif pass print\n' 'identifier fn|identifier let|identifier mut|identifier elif|identifier pass|identifier print|'

# --- literals -------------------------------------------------------------------
expect_tokens ints           '0xFF 0b1010 0o17 1_000_000 0\n' 'integer literal 255|integer literal 10|integer literal 15|integer literal 1000000|integer literal 0|'
expect_tokens int_suffixes   '1u 2l 3ul 4LL 5ull 6lu\n' 'integer literal 1 u|integer literal 2 l|integer literal 3 u l|integer literal 4 ll|integer literal 5 u ll|integer literal 6 u l|'
expect_tokens max_u64        '18446744073709551615\n' 'integer literal 18446744073709551615|'
expect_tokens floats         '1.5 2e3 1_000.25 3E-2 .5 7.\n' 'floating-point literal 1.5|floating-point literal 2000|floating-point literal 1000.25|floating-point literal 0.03|floating-point literal 0.5|floating-point literal 7|'
expect_tokens float_suffix   '1.5f\n' 'floating-point literal 1.5|'
expect_tokens field_after_int 'a[0].b\n' 'identifier a|[|integer literal 0|]|.|identifier b|'
expect_tokens string_escapes '"a\\tb\\x41\\"\\\\"\n' 'string literal "a bA"\" (6 bytes)|'
expect_tokens chars          "'a' '\\\\n' '\\\\x41' '\\\\0'\n" 'char literal byte 97|char literal byte 10|char literal byte 65|char literal byte 0|'
expect_tokens string_concat_tokens '"a" "b"\n' 'string literal "a" (1 bytes)|string literal "b" (1 bytes)|'

expect_error  overflow       '18446744073709551616\n' 'does not fit in 64 bits'
expect_error  bad_suffix     '123abc\n' "invalid character 'a' in numeric literal"
expect_error  bad_binary     '0b102\n' 'malformed base-2'
expect_error  leading_zero   '0755\n' 'for octal write 0o755'
expect_error  float_bad_suffix '1.5x\n' 'invalid suffix on floating-point literal'
expect_error  unterminated_s '"abc\n' 'unterminated string literal'
expect_error  bad_escape     '"\\q"\n' "unknown escape sequence '\\q'"
expect_error  empty_char     "''\n" 'empty char literal'
expect_error  long_char      "'ab'\n" 'exactly one character'
expect_error  unicode_char   "'λ'\n" 'more than one byte'
expect_error  stray_char     'a $ b\n' "unexpected character '\$'"

expect_ok     shebang        $'#!/usr/bin/env cabby\nint main {\n}\n'
expect_ok     example        "$(cat "$ROOT/examples/example.cby")"

echo "lexer tests: $pass passed, $fail failed"
[[ $fail -eq 0 ]]
