" Vim syntax file
" Language:     Rogue
" Maintainer:   Abe Pralle <Abe.Pralle@plasmaworks.com>
" URL:
" Last Change:  2016.12.08

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
syn keyword rogueClassDecl   module
syn keyword rogueClassDecl   class endClass routine endRoutine aspect enum endEnum
syn keyword rogueClassDecl   function endFunction
syn keyword rogueClassDecl   with
syn keyword rogueClassDecl   subclass endSubclass
syn keyword rogueClassDecl   augment endAugment
syn keyword rogueClassDecl   nativeCode nativeHeader endNativeCode endNativeHeader
syn keyword rogueMember      ENUMERATE DEFINITIONS SETTINGS CATEGORIES
syn keyword rogueMember      DEPENDENCIES PROPERTIES METHODS GLOBAL
syn match   rogueError       "\<for\(\s\|(\)"
syn keyword rogueConditional  if elseIf else endIf then
syn keyword rogueConditional  which whichIs case caseNext others endWhich endWhichIs
syn keyword rogueConditional  select
syn keyword rogueConditional  contingent endContingent satisfied unsatisfied
syn keyword rogueConditional  block endBlock
syn keyword rogueConditional  unitTest endUnitTest
syn keyword rogueLoop         await
syn keyword rogueLoop         while endWhile forEach endForEach in of step downTo do
syn keyword rogueLoop         loop endLoop
syn keyword rogueBranch       escapeForEach escapeWhile escapeLoop
syn keyword rogueBranch       escapeTry escapeUse
syn keyword rogueBranch       escapeWhich escapeWhichIs escapeIf escapeContingent escapeBlock
syn keyword rogueBranch       nextIteration
syn keyword rogueBoolean      true false pi
syn keyword rogueConstant     null infinity NaN
syn keyword rogueTypedef      this prior
syn keyword rogueStatement    return necessary sufficient noAction
syn keyword rogueStatement    yield
syn keyword rogueStatement    compileError deprecated
syn match   rogueType         "\$\?\<\u\w*\>\(<<.*>>\)\?\(\[]\)*"
syn match   rogueType         "Function([^()]*)\(->\)\?"
syn keyword rogueScopeDecl    readOnly writeOnly public private limited const local global
syn keyword rogueScopeDecl    singleton
syn keyword rogueStorageClass native macro endMacro essential abstract final compound propagated foreign preferred mutating
syn keyword rogueStorageClass threadLocal synchronized synchronizable
syn keyword rogueStorageClass api nonessential
syn keyword rogueExceptions   throw try catch endTry use endUse
"syn match  roguegPreProc      "^\[.*]"
syn match   rogueDirective    "\$compileArg\>"
syn match   rogueDirective    "\$define\>"
syn match   rogueDirective    "\$defined\>"
syn match   rogueDirective    "\$include\>"
syn match   rogueDirective    "\$if\>"
syn match   rogueDirective    "\$elseIf\>"
syn match   rogueDirective    "\$else\>"
syn match   rogueDirective    "\$endIf\>"
syn match   rogueDirective    "\$essential\>"
syn match   rogueDirective    "\$join\>"
syn match   rogueDirective    "\$macro\>"
syn match   rogueDirective    "\$localDefine\>"
syn match   rogueDirective    "\$localMacro\>"
syn match   rogueDirective    "\$sourceFilepath\>"
syn match   rogueDirective    "\$sourceLine\>"
syn match   rogueDirective    "\$methodSignature\>"
syn keyword rogueDirective    module
syn keyword rogueDirective    uses
syn match   rogueNumber       "\<\(\d\|_\)\+\(\.\(\d\|_\)\+\)\=\>"
syn match   rogueNumber       "\<0b[01_]\+\>"
syn match   rogueNumber       "\<0x\(\x\|_\)\+\>"
syn match   rogueNumber       "\<0c[01234567_]\+\(\.[01234567_]\+\)\=\>"

"syn match   rogueIdentifier  "\h\w*"
"syn match   rogueUserLabelRef  ".\k\+"

syn keyword rogueStatement    require assert
syn keyword rogueStatement    ensure
syn keyword rogueOperator     and or xor not
syn keyword rogueOperator     instanceOf is isReference
"syn match   rogueOperator     "\.\.\<"
"syn match   rogueOperator     "\.\.\>"
"syn match   rogueOperator     "\.\."
"syn keyword rogueOperator     == != <= >=
"syn keyword rogueOperator     + - * / = ^ %
"syn match   rogueOperator    "+"
"syn match   rogueOperator    "-"
"syn match   rogueOperator    "\*"
"syn match   rogueOperator    "/"
"syn match   rogueOperator    "="
"syn match   rogueOperator    "^"
"syn match   rogueOperator    "%"
"syn match   rogueType    ":"
"syn match   rogueType    ";"
"syn match   rogueOperator     "<[^<]"me=e-1
"syn match   rogueOperator     ">[^>]"me=e-1

syn keyword rogueFuncDef  method

syn match   rogueLineComment  "#.*"
syn region  rogueLineComment  start="#{" end="}#"
syn region  rogueString       start=+"+ skip=+\\\\\|\\"+ end=+"+
syn region  rogueString       start=+''+ end=+''+
syn match   rogueString       "'.'"
syn match   rogueString       "'\\.'"

hi def link rogueIdentifier  Identifier
hi def link rogueFuncDef        Function
hi def link rogueMember      Special
hi def link rogueVarArg         Function
hi def link rogueBraces      Function
hi def link rogueBranch      Conditional
hi def link rogueUserLabelRef    rogueUserLabel
hi def link rogueLabel      Label
hi def link rogueUserLabel    Label
hi def link rogueConditional    Conditional
hi def link rogueLoop      Repeat
hi def link rogueExceptions    Exception
hi def link rogueStorageClass    StorageClass
hi def link rogueMethodDecl    rogueStorageClass
hi def link rogueClassDecl    rogueStorageClass
hi def link rogueScopeDecl    rogueStorageClass
hi def link rogueBoolean    Boolean
hi def link rogueSpecial    Special
hi def link rogueSpecialError    Error
hi def link rogueSpecialCharError  Error
hi def link rogueString      String
hi def link rogueCharacter    Character
hi def link rogueSpecialChar    SpecialChar
hi def link rogueNumber      Number
hi def link rogueError      Error
hi def link rogueStringError    Error
hi def link rogueStatement    Statement
"hi def link rogueOperator    Operator
hi def link rogueComment    Comment
hi def link rogueDocComment    Comment
hi def link rogueLineComment    Comment
hi def link rogueConstant    Constant
hi def link rogueTypedef    Typedef
hi def link rogueTodo      Todo
hi def link rogueDirective                PreProc

hi def link rogueCommentTitle    SpecialComment
hi def link rogueDocTags    Special
hi def link rogueDocParam    Function
hi def link rogueDocSeeTagParam    Function
hi def link rogueCommentStar    rogueComment

hi def link rogueType      Type
hi def link rogueExternal    Include

hi def link htmlComment    Special
hi def link htmlCommentPart    Special
hi def link rogueSpaceError    Error

let b:current_syntax = "rogue"

if main_syntax == 'rogue'
  unlet main_syntax
endif

let b:spell_options="contained"

syn sync fromstart

