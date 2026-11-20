" Vim syntax file
" Language:  Cabby
" Files:     *.cby, *.hby

if exists("b:current_syntax")
  finish
endif

let s:cpo_save = &cpo
set cpo&vim

syn case match

" ---- keywords
syn keyword cabbyConditional   if else switch
syn keyword cabbyRepeat        while until do for
syn keyword cabbyRange         in to down by
syn keyword cabbyStatement     return break continue goto
syn keyword cabbyLabel         case default
syn keyword cabbyType          char short int long float double void bool str
syn keyword cabbyType          signed unsigned handler list link lookup file directory process address timer
syn keyword cabbyType          moment duration date time_of_day date_time weekday month json bytes element html xml
syn keyword cabbyStorage       const static register volatile extern inner
syn keyword cabbyStructure     struct union enum object aka callback
syn keyword cabbyBoolean       true false
syn keyword cabbyReserved      cement auto typedef extended quadruple is not and or
syn keyword cabbySelf          self
syn keyword cabbyOperator      sizeof
syn keyword cabbyBuiltin       print println input read_line read_char read_key end_of_input stdout stderr abs close_to random random_seed exit
syn keyword cabbyTodo          contained TODO FIXME XXX NOTE

" use standard / use local path/name
syn match   cabbyUse           "^\s*use\>" nextgroup=cabbyUseLocal,cabbyUsePath skipwhite
syn keyword cabbyUseLocal      contained local nextgroup=cabbyUsePath skipwhite
syn match   cabbyUsePath       contained "[A-Za-z0-9_./-]\+"

" Library names with a namespace: gui.window, gtk.button, game.sprite ...
syn match   cabbyNamespaced    "\<\%([gt]ui\|gtk\|t\=game\|world\)\.\h\w*\>"

" A library's functions: files.read, archives.make, dbus.notify ...
syn match   cabbyLibCall       "\<\(files\|directories\|shell\|addresses\|events\|time\|dates\|json\|bytes\|networks\|html\|xml\|sound\|cabby\|archives\|patterns\|hashes\|databases\|threads\|dbus\|desktop\|power\|volume\|system\|users\|mime\|media\|images\|brightness\|printing\|speech\|bluetooth\|emails\|secrets\|videos\|os\|linux\)\.\h\w*\>"
" and its types (after, so they win): threads.worker, dbus.bus ...
syn match   cabbyLibType       "\<\%(databases\.\%(database\|statement\)\|dbus\.bus\|dbus\.message\|networks\.\%(url\|request\|connection\|server\)\|patterns\.pattern\|sound\.\%(note\|part\|recording\|song\|track\|keyboard\|synthesizer\)\|images\.picture\|threads\.worker\)\>"

" Kinds: `strict long kind duration`, `long kind moment, steps duration`.
" kind, strict and steps are only words there, so only colored there.
syn match   cabbyKindWord      "\%(\<\%(char\|short\|int\|long\|float\|double\|signed\|unsigned\)\s\+\)\@<=kind\ze\s\+\h\w*\s*\%(,\|$\|//\)"
syn match   cabbyKindStrict    "^\s*\zsstrict\ze\s\+\%(\h\w*\s\+\)\+kind\s\+\h\w*\s*\%(,\|$\|//\)"
syn match   cabbyKindWord      "\%(\<kind\s\+\h\w*\s*,\s*\)\@<=steps\>"

" Built-in members: .length .copy .text
syn match   cabbyMember        "\.\@1<=\(length\|copy\|text\|find\|contains\|starts_with\|ends_with\|replace\|trim\|upper\|lower\|repeat\|pad_left\|pad_right\|centered\|split\|join\|add\|insert\|remove_at\|take_last\|clear\|sort\|sort_by\|sort_with\|reverse\|remove\|keys\|values\|read_line\|at_end\|is_int\|is_long\|is_number\|to_int\|to_long\|to_double\|call\|is_set\|value\|is_none\|number\|handle\)\>"

" Destructors: ~name
syn match   cabbyDestructor    "\~\h\w*\(\.\h\w*\)*\ze\s*{"

" Function and method definitions: the name before the parameters on a
" line that opens a block, after a return type.
syn match   cabbyFuncDef       "\%(^\s*\%(\%(static\|const\|extern\|inner\|unsigned\|signed\)\s\+\)*\%(\<\%(static\|const\|extern\|unsigned\|signed\|register\|volatile\|if\|while\|until\|for\|switch\|return\|else\|do\|goto\|case\|object\|struct\|union\|enum\|aka\|use\)\>\)\@!\h[A-Za-z0-9_.]*\%(\s*&\)\=\s\+\)\@<=\h\w*\ze\%(\s\+[^=;(]*\)\=\s*{\s*\%(//.*\)\=$"

" goto labels: a name and a colon alone on a line
syn match   cabbyGotoLabel     "^\s*\h\w*:\s*\(//.*\)\=$" contains=cabbyComment,cabbyLabel

" ---- literals
syn match   cabbyEscape        contained "\\\([nrt0\\'\"]\|x\x\x\)"
syn region  cabbyString        start=+"+ skip=+\\\\\|\\"+ end=+"+ end=+$+ contains=cabbyEscape,cabbyFormat
syn match   cabbyFormat        contained "%[-+ #0]*\d*\(\.\d\+\)\=[hlL]*[diouxXeEfgGcsp%]"
syn match   cabbyChar          "'\([^\\']\|\\\([nrt0\\'\"]\|x\x\x\)\)'"

syn case ignore
syn match   cabbyNumber        "\<\d\+\(u\|l\|ul\|lu\|ll\|ull\)\=\>"
syn match   cabbyNumber        "\<0x\x\+\(u\|l\|ul\|lu\|ll\|ull\)\=\>"
syn match   cabbyNumber        "\<0b[01]\+\(u\|l\|ul\|lu\|ll\|ull\)\=\>"
syn match   cabbyNumber        "\<0o\o\+\(u\|l\|ul\|lu\|ll\|ull\)\=\>"
syn match   cabbyFloat         "\<\d\+\.\d*\(e[-+]\=\d\+\)\=f\="
syn match   cabbyFloat         "\<\d\+e[-+]\=\d\+f\=\>"
syn case match
" C's leading-zero octal is not allowed: write 0o755
syn match   cabbyNumberError   "\<0\d\+\>"

" ---- comments
syn region  cabbyComment       start="//" end="$" contains=cabbyTodo,@Spell keepend
" #!/usr/bin/env cabby on the first line
syn match   cabbyComment       "\%^#!.*$"
syn region  cabbyComment       start="/\*" end="\*/" contains=cabbyTodo,@Spell
" find /* ... */ comments that started above the screen
syn sync ccomment cabbyComment minlines=50

" ---- layout mistakes Cabby rejects
" indentation must be tabs only
syn match   cabbySpaceIndent   "^\t* \+\ze\S"
" statements end at the end of the line: no semicolons
syn match   cabbySemicolon     ";\ze\s*\(//.*\)\=$"

" ---- colors
hi def link cabbyConditional   Conditional
hi def link cabbyRepeat        Repeat
hi def link cabbyRange         Repeat
hi def link cabbyStatement     Statement
hi def link cabbyLabel         Label
hi def link cabbyGotoLabel     Label
hi def link cabbyType          Type
hi def link cabbyNamespaced    Type
hi def link cabbyStorage       StorageClass
hi def link cabbyStructure     Structure
hi def link cabbyKindWord      Structure
hi def link cabbyKindStrict    StorageClass
hi def link cabbyBoolean       Boolean
hi def link cabbySelf          Special
hi def link cabbyOperator      Operator
hi def link cabbyBuiltin       Function
hi def link cabbyLibCall       Function
hi def link cabbyLibType       Type
hi def link cabbyReserved      Error
hi def link cabbyFuncDef       Function
hi def link cabbyDestructor    Function
hi def link cabbyMember        Special
hi def link cabbyUse           Include
hi def link cabbyUseLocal      Include
hi def link cabbyUsePath       String
hi def link cabbyString        String
hi def link cabbyChar          Character
hi def link cabbyEscape        SpecialChar
hi def link cabbyFormat        SpecialChar
hi def link cabbyNumber        Number
hi def link cabbyFloat         Float
hi def link cabbyNumberError   Error
hi def link cabbyComment       Comment
hi def link cabbyTodo          Todo
hi def link cabbySpaceIndent   Error
hi def link cabbySemicolon     Error

let b:current_syntax = "cabby"

let &cpo = s:cpo_save
unlet s:cpo_save
