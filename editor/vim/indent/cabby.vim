" Vim indent file
" Language:  Cabby
"
" Cabby's layout rules: one tab more inside each { block; the closing }
" lines up with the line that opened the block; case labels sit one tab
" inside their switch and their statements one tab further; a line
" continued inside open brackets is indented one tab more than its start.

if exists("b:did_indent")
  finish
endif
let b:did_indent = 1

setlocal autoindent
setlocal nosmartindent nocindent
setlocal indentexpr=CabbyIndent(v:lnum)
setlocal indentkeys=0{,0},0),0],:,!^F,o,O,e,=case,=default

let b:undo_indent = "setlocal ai< si< cin< inde< indk<"

if exists("*CabbyIndent")
  finish
endif

" Is the character at (lnum, col) inside a string, char or comment?
function! s:IsIgnored(lnum, col) abort
  let name = synIDattr(synID(a:lnum, a:col, 0), "name")
  return name =~# '\v^cabby(String|Char|Comment|Escape|Format|Todo)$'
endfunction

" A line with strings, chars and comments removed, so brackets in them
" don't count. Works without syntax highlighting too.
function! s:Code(lnum) abort
  let line = getline(a:lnum)
  let line = substitute(line, '\v"(\\.|[^"\\])*"', '""', 'g')
  let line = substitute(line, '\v''(\\.|[^''\\])''', "''", 'g')
  let line = substitute(line, '//.*$', '', '')
  let line = substitute(line, '/\*.\{-}\*/', '', 'g')
  return line
endfunction

" Is the line nothing but comment (including the middle or end of a
" /* ... */ comment that started earlier)?
function! s:IsCommentLine(lnum) abort
  if s:Code(a:lnum) =~ '^\s*$'
    return 1
  endif
  let line = getline(a:lnum)
  let first = match(line, '\S') + 1
  let last = match(line, '\S\s*$') + 1
  return first > 0
        \ && synIDattr(synID(a:lnum, first, 0), "name") ==# 'cabbyComment'
        \ && synIDattr(synID(a:lnum, last, 0), "name") ==# 'cabbyComment'
endfunction

" Previous line that holds code (skipping blank lines and whole-line comments).
function! s:PrevCode(lnum) abort
  let l = prevnonblank(a:lnum - 1)
  while l > 0 && s:IsCommentLine(l)
    let l = prevnonblank(l - 1)
  endwhile
  return l
endfunction

" How many ( and [ a line leaves open (negative if it closes more).
function! s:OpenBrackets(lnum) abort
  let code = s:Code(a:lnum)
  let opens = len(substitute(code, '[^([]', '', 'g'))
  let closes = len(substitute(code, '[^)\]]', '', 'g'))
  return opens - closes
endfunction

" The line where the statement containing lnum starts: while the start of
" the line is inside an unclosed ( or [, go back to the line that opened it.
function! s:StatementStart(lnum) abort
  let save = getpos('.')
  let l = a:lnum
  let skip = 's:IsIgnored(line("."), col("."))'
  while 1
    call cursor(l, 1)
    let stop = max([1, l - 100])
    let p1 = searchpair('(', '', ')', 'bnW', skip, stop)
    let p2 = searchpair('\[', '', '\]', 'bnW', skip, stop)
    let o = max([p1, p2])
    if o <= 0 || o >= l
      break
    endif
    let l = o
  endwhile
  call setpos('.', save)
  return l
endfunction

" Indent of the line holding the { that the } at (lnum, col) closes.
function! s:MatchingOpen(lnum, col) abort
  let save = getpos('.')
  call cursor(a:lnum, a:col)
  let m = searchpair('{', '', '}', 'bW', 's:IsIgnored(line("."), col("."))')
  call setpos('.', save)
  return m
endfunction

function! s:IsCaseLabel(text) abort
  return a:text =~# '^\s*\(case\>.*\|default\s*\):\s*$'
endfunction

function! CabbyIndent(lnum) abort
  let cur = s:Code(a:lnum)
  let sw = shiftwidth()

  " Inside a /* ... */ comment that began on an earlier line: leave the
  " line as it is.
  let first = match(getline(a:lnum), '\S') + 1
  if first > 0 && getline(a:lnum) !~ '^\s*\(/[/*]\)'
        \ && synIDattr(synID(a:lnum, first, 0), "name") ==# 'cabbyComment'
    return indent(a:lnum)
  endif

  " A line starting with } lines up with the line that opened the block
  " (including `} else {` and `} while cond`).
  if cur =~ '^\s*}'
    let m = s:MatchingOpen(a:lnum, match(getline(a:lnum), '}') + 1)
    if m > 0
      return indent(s:StatementStart(m))
    endif
  endif

  " A case or default label goes one tab inside its switch.
  if s:IsCaseLabel(cur)
    let m = s:MatchingOpen(a:lnum, 1)
    if m > 0
      return indent(s:StatementStart(m)) + sw
    endif
  endif

  let prev = s:PrevCode(a:lnum)
  if prev == 0
    return 0
  endif
  let ptext = s:Code(prev)
  let open = s:OpenBrackets(prev)

  " Inside brackets left open: one tab more than the statement's start.
  if open > 0
    return indent(s:StatementStart(prev)) + sw
  endif

  " The line before closed brackets opened on an earlier line: go back
  " to where that statement started.
  let base = indent(open < 0 ? s:StatementStart(prev) : prev)

  " After a line that opens a block, or a case label: one tab more.
  if ptext =~ '{\s*$' || s:IsCaseLabel(ptext)
    return base + sw
  endif
  return base
endfunction
