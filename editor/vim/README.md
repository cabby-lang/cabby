# Cabby for Vim

Colors for every keyword, type, string, number and comment in `.cby` and
`.hby` files; automatic indenting with tabs, the way Cabby requires; and
red marks on three mistakes Cabby rejects (spaces used for indenting, a
semicolon at the end of a line, and C-style octal like `0755`).

## Install

From Cabby's source folder, **without sudo** (it installs for your user):

```sh
make install-vim                              # Vim: into ~/.vim
make install-vim VIMDIR=~/.config/nvim        # Neovim
```

`make uninstall-vim` (with the same `VIMDIR`) removes it again.

Vim needs filetype detection, plugins, indenting and syntax switched on.
Most Linux Vims do that when you have no `~/.vimrc`, but macOS's own Vim
starts with them all off. If you have a `~/.vimrc`, or you're on a Mac,
make sure it contains (`make install-vim` says so when it doesn't):

```vim
filetype plugin indent on
syntax on
```

## Use

* Open any `.cby` or `.hby` file.
* Indenting follows the language: Enter after `{` goes one tab in, typing
  `}` lines up with the line that opened the block, `case` labels sit one
  tab inside their `switch`, and a line continued inside open parentheses or square brackets goes
  one tab in.
* `gg=G` re-indents a whole file; `=` re-indents a selection.
* Tab is a real tab, shown 8 columns wide. The file always holds real
  tabs, so this only changes how wide they look. To see them narrower, add
  `autocmd FileType cabby setlocal tabstop=4` to your vimrc.
* `gc`-style comment plugins use `//`.
