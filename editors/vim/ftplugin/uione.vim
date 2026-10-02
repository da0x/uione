" Copyright 2026 Daher Alfawares
" SPDX-License-Identifier: AGPL-3.0-only

if exists("b:did_ftplugin")
  finish
endif
let b:did_ftplugin = 1

setlocal commentstring=//\ %s
setlocal comments=s1:/*,mb:*,ex:*/,://
setlocal formatoptions-=t formatoptions+=croql
" Indentation is tabs (decision 0011); four columns wide unless you choose otherwise.
setlocal noexpandtab tabstop=4 shiftwidth=0 softtabstop=0
setlocal autoindent smartindent

let b:undo_ftplugin = "setlocal commentstring< comments< formatoptions< expandtab< tabstop< shiftwidth< softtabstop< autoindent< smartindent<"
