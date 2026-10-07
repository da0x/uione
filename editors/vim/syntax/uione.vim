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
syn match uioneDeclare "^\s*\zs\<\%(project\|environment\|entity\|command\|view\|role\|function\|screen\|picker\|webhook\|backend\)\>\%(::\|\s*=\)\@!" nextgroup=uioneQualifier,uioneName skipwhite
syn match uioneDeclare "^\s*\zs\<namespace\>\%(::\|\s*=\)\@!" nextgroup=uioneNamespaceName skipwhite
syn match uioneDeclare "^\s*\zs\<format\>\ze\s" nextgroup=uioneFormatName skipwhite
syn match uioneNamespaceName "\h\w*\%(::\h\w*\)*" contained contains=uioneScope
syn match uioneName       "\h\w*" contained
" Defined after uioneName: of two matches at one position, Vim takes the later.
syn match uioneQualifier  "\%(\h\w*::\)\+" contained nextgroup=uioneName
syn match uioneFormatName "\h\w*" contained nextgroup=uionePattern skipwhite
syn match uionePattern    "[^[:space:]{]\+" contained

" Words inside blocks
syn keyword uioneKeyword   require permission clear create readers when add remove to has of limit for by on component order each per where from table form confirm hint
syn keyword uioneKeyword   hero section menu link markdown thread timeline copy details layout main side at
" each change of issue: change is the language's own record of an entity's changes,
" a type, as text is.
syn match   uioneType      "\<change\>\ze\s\+of\>"
" history only in an entity's header: entity issue history {.
syn match   uioneKeyword   "\%(^\s*entity\s\+\h\w*\s\+\)\@<=\<history\>"
syn keyword uioneKeyword   example nextgroup=uioneLiteral skipwhite
syn keyword uioneStatement if else return
" A field's rules, only in an entity's block; elsewhere these words are names, like a
" column called after.
syn keyword uioneModifier  contained required unique after key
" public is a view's modifier, before its block or when, and otherwise a choice's
" name, as in visibility enum public | private, which stays plain.
syn match   uioneModifier  /\%(::\||\s*\|\<enum\s\+\)\@<!\<public\>\%(\s*\%({\|$\|when\>\)\)\@=/
syn keyword uioneBuiltin   now me none true false
" Who may run a command: words only after `permission`, so a field called owner
" stays a name.
syn match   uioneBuiltin   "\%(\<permission\s\+\)\@<=\%(anyone\|authenticated\|owner\)\>"
syn match   uioneLiteral   "\S.*$" contained

syn match uioneCall     "\<\h\w*\ze\s*("
syn match uioneConstant "\<\u\w*\>"
syn match uioneNumber   "\<\d\+\%(\.\d\+\)\=\>"
syn match uioneOperator "==\|!=\|<=\|>=\|&&\|||\|[-+*<>=!|]"

" An entity's block: each line is a field, its name first, then its type, built in
" or another entity, so `project  project  required  key` is a field named project
" holding a project, not a declaration. Inside it, words that start declarations
" elsewhere are names.
syn region uioneFields start="\%(^\s*entity\s\+\h\w*\%(\s\+history\)\=\s*\)\@<={" end="^\s*}" contains=uioneFieldName,uioneComment,uioneString,uioneModifier,uioneBuiltin,uioneNumber,uioneOperator,uioneNamespace,uioneScope,uioneFieldWord,uioneListOf
syn match   uioneFieldName "^\s*\zs\h\w*" contained nextgroup=uioneUserType,uioneType skipwhite
syn match   uioneUserType  "\h\w*\%(::\h\w*\)*" contained
syn match   uioneFieldWord "\<\%(per\|of\)\>" contained
" A list's type is one type, read as a unit: list of text, list of label. Defined
" after uioneFieldWord, so of in a list's type is the type's own.
syn match   uioneListOf    "\%(^\s*\h\w*\s\+list\s\+\)\@<=of\>" contained nextgroup=uioneListType,uioneUserType skipwhite
syn match   uioneListType  "\<\%(text\|markdown\|email\|slug\|date\|number\|serial\|boolean\|user\)\>" contained

" A type only where a field line puts one: `startDate date required`. Elsewhere
" these words are names (a field called email, a column called text).
syn match uioneType "\%(^\s*\h\w*\s\+\)\@<=\%(text\|markdown\|email\|slug\|date\|number\|serial\|boolean\|user\|list\|enum\)\>"

" A name given a value with =, like title = issue.title in a view or status =
" status::closed in a command: the field it sets.
syn match uioneAssigned "\%(\.\|\w\)\@<!\<\h\w*\ze\s*=\%(=\)\@!"
" What follows a dot is a field of what's before it, like title in issue.title, even
" a word that's a keyword elsewhere. Starting at the dot, it comes before any keyword.
syn match uioneDotted "\%(\w\)\@<=\.\h\w*"

" A project's block: its settings, each a word at the start of its line, and its
" environments, whose blocks hold settings too. Elsewhere those words are names, like
" a column called title.
syn cluster uioneProjectItems contains=uioneSetting,uioneProjectBraces,uioneComment,uioneString,uioneDeclare,uioneConstant,uioneNumber,uioneOperator,uioneBuiltin
syn region uioneProject matchgroup=uioneProjectBrace start="\%(^\s*project\s\+\h\w*\s*\)\@<={" end="}" contains=@uioneProjectItems
syn region uioneProjectBraces matchgroup=uioneProjectBrace start="{" end="}" contained contains=@uioneProjectItems
syn match  uioneSetting "^\s*\zs\<\%(domain\|firebase\|region\|ui\|authentication\|icon\|color\|layout\|serve\|redirect\|title\|one\|analytics\)\>" contained

" link namespace projects "See the projects": the namespace a link opens.
syn match uioneKeyword "\%(\<link\s\+\)\@<=namespace\>" nextgroup=uioneNamespaceName skipwhite

" `text` and `code` are page content only when a string follows them.
syn match uioneKeyword "\<\%(text\|code\)\ze\s\+\""

" library::book, waitlist::signup::create
syn match uioneNamespace "\<\h\w*\ze::"
syn match uioneScope     "::" nextgroup=uioneMember
" What follows a qualifier is a name, even a word that's a keyword elsewhere, like
" create in member::create.
syn match uioneMember    "\h\w*" contained

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
hi def link uioneSetting     Statement
hi def link uioneAssigned    Identifier
hi def link uioneType        Type
hi def link uioneUserType    Type
hi def link uioneFieldName   Identifier
hi def link uioneFieldWord   Statement
hi def link uioneListOf      Type
hi def link uioneListType    Type
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
