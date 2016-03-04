" Vim indent file
" Language:    Rogue
" Maintainer:  Ty Heath <ty.heath@gmail.com>
" Created:     2009 Aug 5
" Last Change: 2009 Aug 5

" This is my attempt at an indent file for the Rogue programming language.  
" I do not know the vim scripting language, so many things are not written as elegantly as possible.
" Everything should be logically correct though

" Don't override another indent script
if exists("b:did_indent")
  finish
endif
let b:did_indent = 1

" The function vim should call to determine indentation for any particular line
setlocal indentexpr=GetRogueIndent(v:lnum)

" if a line contains any of these, vim calls the indent function as soon as the last letter of that word is typed
setlocal indentkeys&
setlocal indentkeys+=forEach,=endForEach,=while,=endWhile,=nextIteration,=escapeForEach,=escapeWhile
setlocal indentkeys+=if,=elseIf,=endIf
setlocal indentkeys+=delegate,=endDelegate
setlocal indentkeys+=which,=endWhich
setlocal indentkeys+=method,=task,=alias,=class,=endClass,=augment,=endAugment,=aspect,=endAspect,=underlying,=enum,=endEnum
setlocal indentkeys+=compound,=deferred,=singleton,=requisite,=protected
setlocal indentkeys+=PROPERTIES,=METHODS,=INTERNAL,=EXTERNAL,=SETTINGS,=ENUMERATE


" Don't redefine the function over and over
if exists("*GetRogueIndent")
  finish
endif

function! GetPrevNonBlank(startline)
  let s:lnum = a:startline
  while s:lnum > 1
    " a blank line can be any number of #'s followed by any number of white space characters
    if getline(s:lnum) =~ '^#*\s*$' 
      let s:lnum = s:lnum - 1
    else
      return s:lnum
    endif
  endwhile
  return s:lnum
endfunction

function! FindIndentOfPrevWhich(startline)
  let s:lnum = a:startline
  let s:depth=0
  while s:lnum > 1
    if getline(s:lnum) =~ '^\s*endWhich\>'  "we found a nested endWhich
      let s:depth=s:depth + 1
    endif
    if getline(s:lnum) =~ '^\s*which\>' 
      if(s:depth>0)
        let s:depth= s:depth-1
        let s:lnum=s:lnum-1
      else
        return indent(s:lnum)
      endif
    else
      let s:lnum=s:lnum-1
    endif
  endwhile
  return 0
endfunction

function! FindIndentOfPrevDelegate(startline)
  let s:lnum = a:startline
  let s:depth=0
  while s:lnum > 1
    let s:code_line=getline(s:lnum)

    if s:code_line =~ '^\s*endDelegate\>'  "we found a nested delegate
      let s:depth=s:depth + 1
    elseif s:code_line =~ '^\s*delegate\>' 
      if IsSingleLineCond(s:code_line)==0
        if s:depth>0 
          let s:depth=s:depth-1
        else
          return indent(s:lnum)
        endif
      endif
    endif
    let s:lnum=s:lnum-1
  endwhile
  return 0
endfunction

function! FindIndentOfPrevIf(startline)
  let s:lnum = a:startline
  let s:depth=0
  while s:lnum > 1
    let s:code_line=getline(s:lnum)

    if s:code_line =~ '^\s*endIf\>'  "we found a nested if
      let s:depth=s:depth + 1
    elseif s:code_line =~ '^\s*if\>' 
      if IsSingleLineCond(s:code_line)==0
        if s:depth>0 
          let s:depth=s:depth-1
        else
          return indent(s:lnum)
        endif
      endif
    endif
    let s:lnum=s:lnum-1
  endwhile
  return 0
endfunction

function! FindIndentOfPrevForEach(startline)
  let s:lnum = a:startline
  let s:depth=0
  while s:lnum > 1
    let s:code_line=getline(s:lnum)

    if s:code_line =~ '^\s*endForEach\>'  "we found a nested forEach
      let s:depth=s:depth + 1
    elseif s:code_line =~ '^\s*forEach\>' 
      if IsSingleLineCond(s:code_line)==0
        if s:depth>0 
          let s:depth=s:depth-1
        else
          return indent(s:lnum)
        endif
      endif
    endif
    let s:lnum=s:lnum-1
  endwhile
  return 0
endfunction

function! FindIndentOfPrevWhile(startline)
  let s:lnum = a:startline
  let s:depth=0
  while s:lnum > 1
    let s:code_line=getline(s:lnum)

    if s:code_line =~ '^\s*endWhile\>'  "we found a nested forEach
      let s:depth=s:depth + 1
    elseif s:code_line =~ '^\s*while\>' 
      if IsSingleLineCond(s:code_line)==0
        if s:depth>0 
          let s:depth=s:depth-1
        else
          return indent(s:lnum)
        endif
      endif
    endif
    let s:lnum=s:lnum-1
  endwhile
  return 0
endfunction

function! FindIndentOfPrevTry(startline)
  let s:lnum = a:startline
  let s:depth=0
  while s:lnum > 1
    if getline(s:lnum) =~ '^\s*endTry\>'  "we found a nested while
      let s:depth=s:depth + 1
    endif
    if getline(s:lnum) =~ '^\s*try\>' 
      if(s:depth>0)
        let s:depth= s:depth-1
        let s:lnum=s:lnum-1
      else
        return indent(s:lnum)
      endif
    else
      let s:lnum=s:lnum-1
    endif
  endwhile
  return 0
endfunction

function! FindIndentOfPrevLoop(startline)
  let s:lnum = a:startline
  let s:depth=0
  while s:lnum > 1
    if getline(s:lnum) =~ '^\s*endLoop\>'  "we found a nested loop
      let s:depth=s:depth + 1
    endif
    if getline(s:lnum) =~ '^\s*loop\>' 
      if(s:depth>0)
        let s:depth= s:depth-1
        let s:lnum=s:lnum-1
      else
        return indent(s:lnum)
      endif
    else
      let s:lnum=s:lnum-1
    endif
  endwhile
  return 0
endfunction

function! FindIndentForCase(startline)
  let s:sw=2
  let s:lnum = a:startline
  let s:depth=0
  while s:lnum > 1
    if getline(s:lnum) =~ '^\s*endWhich\>' 
      let s:depth=s:depth+1
    endif
    if getline(s:lnum) =~ '^\s*which\>' 
      if(s:depth>0)
        let s:depth=s:depth-1
      else
        return indent(s:lnum)+s:sw
      endif
    endif
    if getline(s:lnum) =~ '^\s*case\>' 
      if(s:depth!>0)
        return indent(s:lnum)
      endif
    endif
    let s:lnum=s:lnum-1
  endwhile
  return 1
endfunction

function! IsLineEmpty(_the_code)
  let s:the_code=a:_the_code
  let s:slen=strlen(s:the_code)
  let s:index=0
  while s:index<s:slen
    if s:the_code[s:index]=='#'
      return 1
    endif
    if s:the_code[s:index]!=' '
      return 0
    endif
    let s:index=s:index+1
  endwhile
  return 1
endfunction

"For ifs, whiles etc that have their statements after the condition on the same line
function! IsSingleLineCond(_the_code)
  "returns 1 for true, 0 for false

  let s:the_code = a:_the_code

  let s:slen=strlen(s:the_code)
  let s:sldepth=0
  let s:foundit=0
  let s:rem=''
  let s:index=0
  if s:the_code =~ '^\s*else#'
    return 1
  endif
  if s:the_code =~ '^\s*else\>' && s:the_code !~ '^\s*elseIf'
    "else is special case because it doesn't have ()
    while s:index<(s:slen)
      if(s:foundit<1)
        if s:the_code[s:index]=='e'
          if s:the_code[s:index+1]=='l'
            if s:the_code[s:index+2]=='s'
              if s:the_code[s:index+3]=='e'
                "echo('index is '.s:index.', strlen is '.s:slen.' rem is '.s:rem)
                let s:foundit=1
                let s:index=s:index+3
              endif
            endif
          endif
        endif
      elseif(s:foundit>0)
        let s:rem=s:rem.s:the_code[s:index]
      endif
      let s:index=s:index+1
    endwhile
    "echo('rem is '.s:rem)
  else
    while s:index<s:slen
      if(s:foundit<1)
        if s:the_code[s:index]=='('
          let s:sldepth=s:sldepth+1
        endif
        if s:the_code[s:index]==')'
          let s:sldepth=s:sldepth-1
          if s:sldepth==0
            let s:foundit=1
          endif
        endif
      else
        let s:rem=s:rem.s:the_code[s:index]
      endif
      let s:index=s:index+1
    endwhile
  endif

  " is the remaining line empty.  Empty is all spaces or a comment
  if IsLineEmpty(s:rem)
    return 0
  else 
    return 1
  endif
endfunction

function! GetRogueIndent( line_num )
  " Line 0 always goes at column 0
  if a:line_num == 0
    return 0
  endif

  let s:sw = 2

  let s:this_codeline = getline( a:line_num )

  "previous line number
  let s:prev_line_num = GetPrevNonBlank(a:line_num-1)  

  "previous line of code
  let s:prev_codeline = getline( s:prev_line_num )  

  "previous level of indent
  let s:indnt = indent( s:prev_line_num )  

  if s:this_codeline =~ '^\s*\(task\|alias\|method\)'
    return s:sw*2
  endif
  " anything that starts with one of these keywords should go on column 0
  if s:this_codeline =~ '^\s*\(class\|aspect\|augment\|compound\|endClass\|endAspect\|endAugment\|endCompound\)\>'
    return 0
  endif

  if s:this_codeline =~ '^\s*\(ENUMERATE\|SETTINGS\|PROPERTIES\|METHODS\|INTERNAL\|EXTERNAL\)\>'
    return s:sw
  endif

  if s:prev_codeline =~ '^\s*\(ENUMERATE\|SETTINGS\|PROPERTIES\|METHODS\|INTERNAL\|EXTERNAL\)\>'
    return s:indnt + s:sw
  endif

  if s:this_codeline =~ '^\s*\(endWhich\)\>'
    return FindIndentOfPrevWhich(a:line_num-1)
  endif

  if s:prev_codeline =~ '^\s*\(delegate\)\>'
    return s:indnt + s:sw
  endif

  if s:this_codeline =~ '^\s*\(endDelegate\)\>'
      return FindIndentOfPrevDelegate(a:line_num-1)
  endif


  if s:this_codeline =~ '^\s*\(endIf\)\>'
      return FindIndentOfPrevIf(a:line_num-1)
  endif

  if s:this_codeline =~ '^\s*\(else\|elseIf\)\>'
    "echo('This line is conditional')
    if IsSingleLineCond(s:this_codeline)>0
      "If this is a single line conditional we need to see if previous line was...
      if IsSingleLineCond(s:prev_codeline)>0
        return s:indnt
      else
        return s:indnt+2
      endif
    else
      let s:testval=FindIndentOfPrevIf(a:line_num-1)
      if(s:testval==0)
        return s:indnt
      else
        return s:testval
      endif
    endif
  endif

  if s:this_codeline =~ '^\s*\(endTry\|catch\)\>'
    return FindIndentOfPrevTry(a:line_num-1)
  endif

  if s:this_codeline =~ '^\s*\(endForEach\)\>'
    return FindIndentOfPrevForEach(a:line_num-1)
  endif

  if s:this_codeline =~ '^\s*\(endWhile\)\>'
    return FindIndentOfPrevWhile(a:line_num-1)
  endif

  if s:this_codeline =~ '^\s*\(endLoop\)\>'
    return FindIndentOfPrevLoop(a:line_num-1)
  endif

  if s:this_codeline =~ '^\s*\(method\|task\|alias\)\>'
    return s:sw*2
  endif

  if s:this_codeline =~ '^\s*\(case\|other\)\>'
    return FindIndentForCase(a:line_num-1)
  endif

  if s:prev_codeline =~ '^\s*\(which\)\>'
    return s:indnt + s:sw
  endif

  if s:prev_codeline =~ '^\s*\(case\)\>'
      return s:indnt + s:sw
  endif

  if s:prev_codeline =~ '^\s*\(method\|task\|alias\)\>'
    return s:indnt + s:sw
  endif
  
  if s:prev_codeline =~ '^\s*\(try\|catch\)\>'
    return s:indnt + s:sw
  endif

  if s:prev_codeline =~ '^\s*\(method\|task\|alias\)'
    return s:indnt + s:sw
  endif

  if s:prev_codeline =~ '^\s*\(if\|while\|forEach\|loop\|else\|elseIf\|try\|catch\)\>'
      "echo('Previous is conditional')
    if IsSingleLineCond(s:prev_codeline)>0
      "echo('Prev is Single line cond')
      return s:indnt 
    else
      "echo('Prev is NOT Single line cond, prev = ' s:prev_codeline)
      return s:indnt + s:sw
    endif
  endif

  "if s:prev_codeline =~ '^\s*\<\(while\)\>'
    "return s:indnt + s:sw
  "endif

  "if s:prev_codeline =~ '^\s*\<\(elseIf\)\>'
    "return s:indnt + s:sw
  "endif

  "By default return the indentation of the previous line if no other conditions match
  return s:indnt
endfunction
