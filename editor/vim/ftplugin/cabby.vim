" Vim filetype plugin for Cabby: tabs only, C-style comments.
if exists("b:did_ftplugin")
  finish
endif
let b:did_ftplugin = 1

" Cabby indents with one real tab per level; spaces are an error.
setlocal noexpandtab
setlocal tabstop=8
setlocal shiftwidth=0
setlocal softtabstop=0

setlocal commentstring=//\ %s
setlocal comments=s1:/*,mb:*,ex:*/,://
setlocal formatoptions-=t formatoptions+=croql
setlocal suffixesadd=.cby,.hby
setlocal include=^\\s*use\\s\\+\\(local\\s\\+\\)\\=

let b:undo_ftplugin = "setlocal et< ts< sw< sts< cms< com< fo< sua< inc<"
