" Vim syntax file
" Language:     Rogue
" Maintainer:   Abe Pralle <Abe.Pralle@plasmaworks.com>
" URL:    
" Last Change:  2010.06.03

" Windows: place in c:\Program Files\vim\vimfiles\syntax\
" MacVim:  place in ~/.vim/syntax/

" Quit when a syntax file was already loaded
if !exists("main_syntax")
  if exists("b:current_syntax")
    finish
  endif
  " we define it here so that included files can test for it
  let main_syntax='rogue'
endif

" keyword definitions
syn keyword bardClassDecl   class endClass aspect endAspect compound endCompound enumeration endEnumeration
syn keyword bardClassDecl   delegate endDelegate function endFunction primitive endPrimitive
syn keyword bardClassDecl   with endWith
syn keyword bardClassDecl   augment endAugment framework endFramework
syn keyword bardMember      ENUMERATE DEFINITIONS SETTINGS CATEGORIES
syn keyword bardMember      PROPERTIES METHODS ROUTINES
syn match   bardError       "\<for\(\s\|(\)"
syn keyword bardConditional  if elseIf else endIf
syn keyword bardConditional  which whichIs case caseNext others endWhich endWhichIs
syn keyword bardConditional  contingent endContingent satisfied unsatisfied
syn keyword bardConditional  try catch endTry
syn keyword bardConditional  block endBlock
syn keyword bardLoop         while endWhile forEach endForEach in of step
syn keyword bardLoop         loop endLoop
syn keyword bardBranch       escapeForEach escapeWhile escapeLoop
syn keyword bardBranch       escapeWhich escapeWhichIs escapeIf escapeContingent escapeBlock
syn keyword bardBranch       nextIteration
syn keyword bardBoolean      true false
syn keyword bardConstant     null void gt lt eq infinity NaN
syn keyword bardTypedef      this prior
syn keyword bardStatement    return necessary sufficient noAction
syn keyword bardStatement    yield yieldAndWait yieldWhile withTimeout
syn keyword bardSpecial      insertUnderlying
syn keyword bardType         Integer Byte Character Real Logical null String
syn keyword bardStorageClass overlaying underlying
syn keyword bardScopeDecl    readOnly writeOnly public private limited const local 
syn keyword bardScopeDecl    deferred singleton managed
syn keyword bardStorageClass native inline nativeInline requisite abstract final propagated functional dynamicAccess
syn keyword bardStorageClass marshalling omit
syn keyword bardExceptions   throw try catch finally
syn keyword bardPreProc      useFramework
"syn match  bardgPreProc      "^\[.*]"
syn region  bardPreProc      start="\$\[" end="[\n\]]"
syn match   bardNumber       "\<\d\+\(\.\d\+\)\=\>"
syn match   bardNumber       "\<0b[01]\+\(\.[01]\+\)\=\>"
syn match   bardNumber       "\<0x\x\+\(\.\x\+\)\=\>"

"syn match   bardIdentifier  "\h\w*"
"syn match   bardUserLabelRef  ".\k\+"

syn keyword bardStatement    require ensure assert
syn keyword bardOperator     and or xor not
syn keyword bardOperator     instanceOf is isNot notInstanceOf duplicate
"syn match   bardOperator     "\.\.\<"
"syn match   bardOperator     "\.\.\>"
"syn match   bardOperator     "\.\."
"syn keyword bardOperator     == != <= >= 
"syn keyword bardOperator     + - * / = ^ %
"syn match   bardOperator    "+"
"syn match   bardOperator    "-"
"syn match   bardOperator    "\*"
"syn match   bardOperator    "/"
"syn match   bardOperator    "="
"syn match   bardOperator    "^"
"syn match   bardOperator    "%"
"syn match   bardType    ":"
"syn match   bardType    ";"
"syn match   bardOperator     "<[^<]"me=e-1
"syn match   bardOperator     ">[^>]"me=e-1

syn keyword bardFuncDef  method routine task

syn match   bardLineComment  "#.*" 
syn region  bardLineComment  start="#{" end="}#"
syn region  bardString       start=+"+ skip=+\\\\\|\\"+ end=+"+
syn region  bardString       start=+\/\/+ skip=+\\\/+ end=+\/\/+
syn match   bardString       "'.'"
syn match   bardString       "'\\.'"

hi def link bardIdentifier  Identifier
hi def link bardFuncDef        Function
hi def link bardMember      Special
hi def link bardVarArg         Function
hi def link bardBraces      Function
hi def link bardBranch      Conditional
hi def link bardUserLabelRef    bardUserLabel
hi def link bardLabel      Label
hi def link bardUserLabel    Label
hi def link bardConditional    Conditional
hi def link bardLoop      Repeat
hi def link bardExceptions    Exception
hi def link bardStorageClass    StorageClass
hi def link bardMethodDecl    bardStorageClass
hi def link bardClassDecl    bardStorageClass
hi def link bardScopeDecl    bardStorageClass
hi def link bardBoolean    Boolean
hi def link bardSpecial    Special
hi def link bardSpecialError    Error
hi def link bardSpecialCharError  Error
hi def link bardString      String
hi def link bardCharacter    Character
hi def link bardSpecialChar    SpecialChar
hi def link bardNumber      Number
hi def link bardError      Error
hi def link bardStringError    Error
hi def link bardStatement    Statement
"hi def link bardOperator    Operator
hi def link bardComment    Comment
hi def link bardDocComment    Comment
hi def link bardLineComment    Comment
hi def link bardConstant    Constant
hi def link bardTypedef    Typedef
hi def link bardTodo      Todo
hi def link bardPreProc                PreProc

hi def link bardCommentTitle    SpecialComment
hi def link bardDocTags    Special
hi def link bardDocParam    Function
hi def link bardDocSeeTagParam    Function
hi def link bardCommentStar    bardComment

hi def link bardType      Type
hi def link bardExternal    Include

hi def link htmlComment    Special
hi def link htmlCommentPart    Special
hi def link bardSpaceError    Error

let b:current_syntax = "rogue"

if main_syntax == 'rogue'
  unlet main_syntax
endif

let b:spell_options="contained"

syn sync fromstart

