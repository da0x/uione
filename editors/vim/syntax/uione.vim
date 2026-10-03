" Copyright 2026 Daher Alfawares
" SPDX-License-Identifier: AGPL-3.0-only

" Vim syntax file
" Language: uione
" Files:    *.one
"
" Kept in step with ../../vscode/syntaxes/uione.tmlanguage.json by ../../test.mjs.

if exists("b:current_syntax")
  finish
endif

syn sync fromstart

" Comments
syn keyword uioneTodo contained TODO FIXME XXX NOTE
syn match   uioneComment "//.*$" contains=uioneTodo,@Spell
syn region  uioneComment start="/\*" end="\*/" contains=uioneTodo,@Spell

" Declarations open a line; the word after them is the thing being declared.
syn match uioneDeclare "^\s*\zs\<\%(project\|entity\|command\|view\|role\|function\|screen\|picker\|webhook\|backend\)\>" nextgroup=uioneQualifier,uioneName skipwhite
syn match uioneDeclare "^\s*\zs\<namespace\>" nextgroup=uioneNamespaceName skipwhite
syn match uioneDeclare "^\s*\zs\<format\>\ze\s" nextgroup=uioneFormatName skipwhite
syn match uioneNamespaceName "\h\w*\%(::\h\w*\)*" contained contains=uioneScope
syn match uioneName       "\h\w*" contained
" Defined after uioneName: of two matches at one position, Vim takes the later.
syn match uioneQualifier  "\%(\h\w*::\)\+" contained nextgroup=uioneName
syn match uioneFormatName "\h\w*" contained nextgroup=uionePattern skipwhite
syn match uionePattern    "[^[:space:]{]\+" contained

" Words inside blocks
syn keyword uioneKeyword   require permission clear create readers when add remove to has of change history limit for by on component order each per where from table form confirm hint
syn keyword uioneKeyword   hero section menu link markdown domain firebase region ui signin icon serve redirect at
" title is a project setting when a string follows it, and a field's name otherwise.
syn match   uioneKeyword   /\<title\>\ze\s\+"/
syn keyword uioneKeyword   example nextgroup=uioneLiteral skipwhite
syn keyword uioneStatement if else return
syn keyword uioneModifier  required unique after key public
syn keyword uioneBuiltin   now me none true false
" Who may run a command: words only after `permission`, so a field called owner
" stays a name.
syn match   uioneBuiltin   "\%(\<permission\s\+\)\@<=\%(anyone\|signed_in\|owner\)\>"
syn match   uioneLiteral   "\S.*$" contained

syn match uioneCall     "\<\h\w*\ze\s*("
syn match uioneConstant "\<\u\w*\>"
syn match uioneNumber   "\<\d\+\%(\.\d\+\)\=\>"
syn match uioneOperator "==\|!=\|<=\|>=\|&&\|||\|[-+*<>=!|]"

" A type only where a field line puts one: `startDate date required`. Elsewhere
" these words are names (a field called email, a column called text).
syn match uioneType "\%(^\s*\h\w*\s\+\)\@<=\%(text\|markdown\|email\|slug\|date\|number\|serial\|boolean\|user\|list\)\>"

" link namespace projects "See the projects": the namespace a link opens.
syn match uioneKeyword "\%(\<link\s\+\)\@<=namespace\>" nextgroup=uioneNamespaceName skipwhite

" `text` and `code` are page content only when a string follows them.
syn match uioneKeyword "\<\%(text\|code\)\ze\s\+\""

" library::book, waitlist::signup::create
syn match uioneNamespace "\<\h\w*\ze::"
syn match uioneScope     "::"

" Routes: /, /docs/:page, and #places on a page
syn match uioneRoute "\%(^\|\s\)\zs/\%([A-Za-z:][A-Za-z0-9_:/-]*\)\=\ze\%(\s\|{\|$\)" contains=uioneParam
syn match uioneRoute "\%(^\|\s\)\zs#\h[A-Za-z0-9_-]*"
syn match uioneParam ":\h\w*" contained

" Strings, with {expressions} inside
syn region uioneString start=+"+ skip=+\\.+ end=+"+ contains=uioneInterp
syn region uioneInterp matchgroup=uioneInterpDelim start="{" end="}" contained contains=uioneCall,uioneBuiltin,uioneConstant,uioneNumber,uioneOperator,uioneNamespace,uioneScope

hi def link uioneComment     Comment
hi def link uioneTodo        Todo
hi def link uioneDeclare     Keyword
hi def link uioneName        Function
hi def link uioneNamespaceName Type
hi def link uioneQualifier   Type
hi def link uioneNamespace   Type
hi def link uioneScope       Delimiter
hi def link uioneFormatName  Function
hi def link uionePattern     Special
hi def link uioneKeyword     Statement
hi def link uioneStatement   Statement
hi def link uioneModifier    StorageClass
hi def link uioneType        Type
hi def link uioneBuiltin     Constant
hi def link uioneLiteral     String
hi def link uioneCall        Function
hi def link uioneConstant    Constant
hi def link uioneNumber      Number
hi def link uioneOperator    Operator
hi def link uioneRoute       Special
hi def link uioneParam       Identifier
hi def link uioneString      String
hi def link uioneInterpDelim Special

let b:current_syntax = "uione"
