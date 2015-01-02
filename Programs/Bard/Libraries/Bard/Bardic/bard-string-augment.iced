#==============================================================================
# bard-string-augment.iced
#
# Adds a number of Bard language-style augments to the standard JavaScript 
# String.
#
# Created July 5, 2014 by Abe Pralle
#
# This is free and unencumbered software released into the public domain under
# the terms of the UNLICENSE at http://unlicense.org.
#==============================================================================
String.prototype.after_first = (st) ->
  i = this.indexOf( st )
  if (i == -1) then return ""
  return this.substring( i + st.length )

String.prototype.after_last = (st) ->
  i = this.lastIndexOf( st )
  if (i == -1) then return ""
  return this.substring( i + st.length )

String.prototype.before_first = (st) ->
  i = this.indexOf( st )
  if (i == -1) then return this.toString()
  return this.substring( 0, i )

String.prototype.before_last = (st) ->
  i = this.lastIndexOf( st )
  if (i == -1) then return this.toString()
  return this.substring( 0, i )

String.prototype.begins_with = (other) ->
  other_count = other.length
  return (this.length >= other_count and this.substring_equals(0,other))

String.prototype.ends_with = (other) ->
  this_count = this.length
  other_count = other.length
  if (other_count > this_count) then return false
  return this.substring_equals( this_count-other_count, other )

String.prototype.from_first = (st) ->
  i = this.indexOf( st )
  if (i == -1) then return ""
  return this.substring( i )

String.prototype.from_last = (st) ->
  i = this.lastIndexOf( st )
  if (i == -1) then return ""
  return this.substring( i )

String.prototype.indent = (n) ->
  if ( !n ) then return this.toString()

  indent = " ".times(n)
  result = ""
  for line in this.split('\n')
    if (result.length) then result += "\n"
    if (line.length) then result += indent + line
  return result

String.prototype.is_letter = () ->
  code = this.charCodeAt(0)
  return ((code >= 97 and code <= 122) or (code >= 65 and code <= 90))

String.prototype.is_number = () ->
  code = this.charCodeAt(0)
  return (code >= 48 and code <= 57)

String.prototype.leftmost = (n) ->
  # "bookkeepers".leftmost(4)  -> "book"
  # "bookkeepers".leftmost(-1) -> "bookkeeper"
  len = this.length
  if (n >= len) then return this.toString()
  if (n < 0) then return this.substring( 0, len + n )

  return this.substring( 0, n )

String.prototype.leftmost_equals = ( n, other ) ->
  this_count = this.length
  other_count = other.length

  if (n <= 0) then return true
  if (n > this_count or n > other_count) then return false

  i = 0
  while (i < n)
    if (this[i] != other[i]) then return false
    ++i

  return true

String.prototype.rightmost = (n) ->
  # "bold move".rightmost(4)  -> "move"
  # "bold move".rightmost(-1) -> "old move"
  len = this.length
  if (n >= len) then return this.toString()
  if (n < 0) then return this.substring(-n)

  return this.substring( len - n )

String.prototype.rightmost_equals = ( n, other ) ->
  this_count = this.length
  other_count = other.length

  if (n <= 0) then return true
  if (n > this_count or n > other_count) then return false

  this_i1 = this_count - n
  other_i1 = other_count - n
  i = 0
  while (i < n)
    if (this[this_i1+i] != other[other_i1+i]) then return false
    ++i

  return true

String.prototype.substring_equals = ( i1, other ) ->
  this_count = this.length
  other_count = other.length

  if (i1 < 0) then i1 = 0
  if (i1 >= this_count) then return (other_count == 0)

  j = 0
  while (j < other_count)
    if (this[i1+j] != other[j]) then return false
    ++j

  return true

String.prototype.times = (n) ->
  if (n <= 0) then return ""

  st = this.toString()
  result = st
  times = 1
  next_p2 = 2
  while (next_p2 <= n)
    result = result + result
    times = next_p2
    next_p2 <<= 1

  while (times++ < n)
    result += st

  return result

String.prototype.hex_character_to_number = (i) ->
  if (@length == 0) then return -1

  ch = @charCodeAt(i)

  if (ch >= 48 and ch <= 57)  then return ch - 48
  if (ch >= 65 and ch <= 70)  then return (ch - 65) + 10
  if (ch >= 97 and ch <= 102) then return (ch - 97) + 10
  return -1

String.prototype.word_wrap = (width,newline='\n') ->
    # Returns a word-wrapped version of this string.  Existing newlines 
    # characters will cause a new line to begin immediately.  Spaces 
    # immediately following existing newline characters are preserved - in 
    # other words, indentation is preserved.
    result = ""
    i1 = i2 = 0

    len = this.length

    if (len == 0)
      return result

    needs_newline = false
    while (i2 < len)
      # find last space or first \n
      while ((i2-i1) < width and i2 < len and this[i2] != '\n')
        i2++

      if ((i2-i1) == width)

        if (i2 >= len)
          i2 = len

        else
          while (this[i2]!=' ' and this[i2]!='\n' and i2>i1)
            i2--

          if (i2 == i1)
            i2 = i1 + width

      if (needs_newline)
        result += newline

      i = i1
      while (i < i2)
        result += this[i++]
      needs_newline = true

      if (i2 == len)
        return result
      else
        switch (this[i2])
          when ' '
            while (i2<len and this[i2]==' ')
              i2++

            if (i2<len and this[i2]=='\n')
              i2++

          when '\n'
            i2++

        i1 = i2

    return result

