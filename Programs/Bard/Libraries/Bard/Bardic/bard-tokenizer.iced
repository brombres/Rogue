fs = require("fs")
require("./bard-string-augment")

#==============================================================================
# ParseError
#==============================================================================
class ParseError
  constructor: (@message,@filepath,@line,@column) ->

  toString: () ->
    buffer  = "=".times(79) + "\n"
    buffer += "ERROR"
    if (@filepath)
      buffer += " in #{@filepath}"
      if (@line)
        buffer += " line #{@line}"
        if (@column) then buffer += ", column #{@column}"
    buffer += "\n\n"

    buffer += @message.word_wrap(77).indent(2)
    buffer += "\n"

    buffer += "=".times(79) + "\n"
    return buffer

#==============================================================================
# ScanNode
#==============================================================================
class ScanNode
  constructor: () ->
    @next_map = {}

  on_accept: null

  parse: (scanner,num_ahead=0) ->
    ch = scanner.peek( num_ahead )
    next = @next_map[ ch ]
    if (next)
      t = next.parse( scanner, num_ahead+1 )
      if (t)
        scanner.read()
        return t

    if (@on_accept)
      return @on_accept()

    return null

  define: (text,type,i=0) ->
    if (i == text.length)
      if (typeof(type) == "number")
        @on_accept = () -> return new Token( type )
      else
        @on_accept = type
    else
      next = @next_map[ text[i] ]
      if (not next)
        next = new ScanNode()
        @next_map[ text[i] ] = next
      next.define( text, type, i+1 )

#==============================================================================
# Scanner
#==============================================================================
class Scanner
  is_scanner     : true

  keep_tabs      : false
  # Can specify in constructor 'options'.

  spaces_per_tab : 2
  # Defines either the number of spaces each tab is converted to (if 
  # 'keep_tabs' is false) or the number of columns each tab counts as
  # during parsing.  Can specify in constructor 'options'.

  line           : 1
  column         : 1
  position       : 0
  count          : 0

  constructor: (@filepath,options) ->
    if (options and options.spaces_per_tab != undefined)
      @spaces_per_tab = options.spaces_per_tab

    @keep_tabs = !!(options and options.keep_tabs)

    if (@keep_tabs) then @read = @read_with_tabs

    try
      if (options and options.data)
        @data = options.data
      else
        @data = fs.readFileSync( @filepath, "utf8" )
      @prepare_data()
    catch err
      throw new ParseError( 'Error opening "' + @filepath + '" for reading.', @filepath )

  consume: (st) ->
    len = st.length
    if (st.length == 1)
      if (@peek() != st) then return false
      @read()
      return true
    else
      for i of st
        if (@peek(i) != st[i]) then return false

      for i of st
        @read()

      return true

  consume_spaces: () ->
    found = false

    while (@consume(' '))
      found = true

    return found

  has_another: () ->
    return (@position < @count)

  peek: (num_ahead) ->
    # 0-based
    if (num_ahead)
      peek_pos = @position + num_ahead
      if (peek_pos >= @count) then return '\0'
      return @data[peek_pos]
    else
      if (@position == @count) then return '\0'
      return @data[@position]

  prepare_data: () ->
    # Converts spaces to tabs (unless 'spaces_per_tab' is 0) and strips out
    # unicode values 127/delete and 0..31 (except for 10/newline and maybe 9/tab).
    original_data = @data
    filtered_data = ""

    if (@keep_tabs)
      for ch in original_data
        code = ch.charCodeAt(0)
        if ((code >= 32 and code != 127) or code == 10 or code == 9) then filtered_data += ch

    else
      tab_spaces = " ".times(@spaces_per_tab)
    
      for ch in original_data
        code = ch.charCodeAt(0)
        if ((code >= 32 and code != 127) or code == 10) then filtered_data += ch
        else if (code == 9) then filtered_data += tab_spaces

    @data = filtered_data
    @count = @data.length

  read: () ->
    # 'read' is possibly set to read_with_tabs() in the constructor.
    result = @data[ @position++ ]

    if (result == '\n')
      ++@line
      @column = 1
    else
      ++@column

    return result

  read_with_tabs: () ->
    # The constructor possibly sets 'read' to be this method.
    result = @data[ @position++ ]

    if (result == '\n')
      ++@line
      @column = 1
    else if (result == '\t')
      @column += @spaces_per_tab
    else
      ++@column

    return result

#==============================================================================
# TokenType
#==============================================================================
TokenType =
{
  definitions: [
    # GENERAL NON-STATEMENT
    {token_type:"EOL"}
    {token_type:"COMMENT"}

    {token_type:"DEFINITION"}
    {token_type:"LOCAL_DEFINITION"}
    {token_type:"ALIAS_ARG_INDEX"}

    {token_type:"ILLEGAL_KEYWORD"}

    # NON-STATEMENT KEYWORDS
    { keyword: "alias" }
    { keyword: "aspect" }
    { keyword: "augment" }
    { keyword: "catch" }
    { keyword: "CATEGORIES" }
    { keyword: "class" }
    { keyword: "case" }
    { keyword: "compound" }
    { keyword: "else" }
    { keyword: "elseIf" }
    { keyword: "endAspect" }
    { keyword: "endAugment" }
    { keyword: "endClass" }
    { keyword: "endCompound" }
    { keyword: "endContingent" }
    { keyword: "endDelegate" }
    { keyword: "endEnumeration" }
    { keyword: "endForEach" }
    { keyword: "endFunction" }
    { keyword: "endIf" }
    { keyword: "endLoop" }
    { keyword: "endPrimitive" }
    { keyword: "endWhich" }
    { keyword: "endWhichIs" }
    { keyword: "endTry" }
    { keyword: "endWhile" }
    { keyword: "ENUMERATE" }
    { keyword: "enumeration" }
    { keyword: "in" }
    { keyword: "METHODS" }
    { keyword: "method" }
    { keyword: "of" }
    { keyword: "others" }
    { keyword: "primitive" }
    { keyword: "PROPERTIES" }
    { keyword: "satisfied" }
    { keyword: "SHARED" }
    { keyword: "task" }
    { keyword: "unsatisfied" }
    { keyword: "withTimeout" }

    # NON-STATEMENT SYMBOLS
    { SYMBOL_CLOSE_BRACE:      '}'  }
    { SYMBOL_CLOSE_BRACKET:    ']'  }
    { SYMBOL_CLOSE_COMMENT:    "}#" }
    { SYMBOL_CLOSE_PAREN:      ')'  }
    { SYMBOL_CLOSE_SPECIALIZE: ">>" }

    { SYMBOL_COMMA: ',' }

    { token_type: "LAST_NON_STATEMENT" }

    # GENERAL
    { token_type: "COMMENT" }
    { token_type: "IDENTIFIER" }
    { token_type: "TYPE_PLACEHOLDER" }

    # CONTROL
    { keyword: "delegate" }
    { keyword: "escapeContingent" }
    { keyword: "escapeForeach" }
    { keyword: "escapeIf" }
    { keyword: "escapeLoop" }
    { keyword: "escapeTry" }
    { keyword: "escapeWhich" }
    { keyword: "escapeWhile" }
    { keyword: "contingent" }
    { keyword: "forEach" }
    { keyword: "function" }
    { keyword: "if" }
    { keyword: "loop" }
    { keyword: "necessary" }
    { keyword: "nextIteration" }
    { keyword: "prior" }
    { keyword: "return" }
    { keyword: "sufficient" }
    { keyword: "throw" }
    { keyword: "trace" }
    { keyword: "tron" }
    { keyword: "troff" }
    { keyword: "which" }
    { keyword: "whichIs" }
    { keyword: "try" }
    { keyword: "while" }
    { keyword: "yield" }
    { keyword: "yield_and_wait" }
    { keyword: "yield_while" }

    # EXPRESSIONS
    { token_type:"LITERAL_REAL" }
    { token_type:"LITERAL_INTEGER" }
    { token_type:"LITERAL_CHARACTER" }
    { token_type:"LITERAL_STRING" }
    { keyword: "true" }
    { keyword: "false" }

    { keyword: "and" }
    { keyword: "as" }
    { keyword: "downTo" }
    { keyword: "instanceOf" }
    { keyword: "is" }
    { keyword: "isNot" }
    { keyword: "local" }
    { keyword: "not" }
    { keyword: "notInstanceOf" }
    { keyword: "or" }
    { keyword: "pi" }
    { keyword: "this" }
    { keyword: "noAction" }
    { keyword: "null" }
    { keyword: "xor" }

    { SYMBOL_ARROW:                "->" }
    { SYMBOL_AT:                   '@' }
    { SYMBOL_BACKSLASH:            '\\' }
    { SYMBOL_COLON:                ':' }
    { SYMBOL_COMPARE:              '<>' }
    { SYMBOL_DOLLAR:               '$' }
    { SYMBOL_DOWN_TO_GREATER_THAN: "..>" }
    { SYMBOL_EMPTY_BRACES:         '{}' }
    { SYMBOL_EMPTY_BRACKETS:       '[]' }
    { SYMBOL_EQ:                   "==" }
    { SYMBOL_EQUALS:               '=' }
    { SYMBOL_FAT_ARROW:            "=>" }
    { SYMBOL_GE:                   ">=" }
    { SYMBOL_GT:                   '>' }
    { SYMBOL_LE:                   "<=" }
    { SYMBOL_LT:                   '<' }
    { SYMBOL_NE:                   "!=" }
    { SYMBOL_OPEN_BRACE:           '{' }
    { SYMBOL_OPEN_BRACKET:         '[' }
    { SYMBOL_OPEN_DIRECTIVE:       "$[" }
    { SYMBOL_OPEN_PAREN:           '(' }
    { SYMBOL_OPEN_SPECIALIZE:      "<<" }
    { SYMBOL_PERIOD:               '.' }
    { SYMBOL_QUESTION_MARK:        '?' }
    { SYMBOL_SCOPE:                "::" }
    { SYMBOL_SEMICOLON:            ';' }
    { SYMBOL_SHL:                  ":<<:" }
    { SYMBOL_SHR:                  ":>>:" }
    { SYMBOL_SHRX:                 ":>>>:" }
    { SYMBOL_UP_TO:                ".." }
    { SYMBOL_UP_TO_LESS_THAN:      "..<" }

    { token_type: "FIRST_DEFINABLE_OPERATOR" }
    { SYMBOL_PLUS:                 '+' }
    { SYMBOL_MINUS:                '-' }
    { SYMBOL_TIMES:                '*' }
    { SYMBOL_DIVIDE:               '/' }
    { SYMBOL_PERCENT:              '%' }
    { SYMBOL_POWER:                '^' }
    { SYMBOL_AMPERSAND:            '&' }
    { SYMBOL_BITWISE_OR:           '|' }
    { SYMBOL_BITWISE_XOR:          '~' }
    { SYMBOL_BITWISE_NOT:          '!' }
    { SYMBOL_INCREMENT:            "++" }
    { SYMBOL_DECREMENT:            "--" }
    { token_type: "LAST_DEFINABLE_OPERATOR" }

    { token_type: "FIRST_SHORTHAND_OPERATOR" }
    { SYMBOL_ADD_ASSIGN:           "+=" }
    { SYMBOL_SUBTRACT_ASSIGN:      "-=" }
    { SYMBOL_MULTIPLY_ASSIGN:      "*=" }
    { SYMBOL_DIVIDE_ASSIGN:        "/=" }
    { SYMBOL_MOD_ASSIGN:           "%=" }
    { SYMBOL_POWER_ASSIGN:         "^=" }
    { SYMBOL_BITWISE_AND_ASSIGN:   "&=" }
    { SYMBOL_BITWISE_OR_ASSIGN:    "|=" }
    { SYMBOL_BITWISE_XOR_ASSIGN:   "~=" }
    { SYMBOL_ACCESS_ASSIGN:        ".=" }
    { token_type: "LAST_SHORTHAND_OPERATOR" }

    # ILLEGAL KEYWORDS
    #{ illegal_keyword: { keyword:"Integer", message:"Use 'Int', 'Int32', or 'Int64' instead of 'Integer'." } }
  ]
}

#==============================================================================
# Token
#==============================================================================
class Token
  constructor: (@type,@filepath,@line=0,@column=0,value) ->
    if (value) then @value = value

  error: (message) ->
    return new ParseError( message, @filepath, @line, @column )

  identify: () ->
    result = TokenType.id_lookup[ @type ]
    if (@value) then result += " " + @value
    return result

  toString: () ->
    if (@value) then return "" + @value
    return TokenType.name_lookup[ @type ]


#==============================================================================
# Tokenizer
#==============================================================================
Tokenizer =
  line:   0
  column: 0

  consume: (text) ->
    return @scanner.consume( text )

  consume_white_space: () ->
    if (not @scanner.consume(' ')) then return false

    while (@scanner.consume(' ')) then # NOOP
    return true

  convert_characters_to_token: (terminator, text) ->
    t = null
    if (text.length == 1 and terminator == '\'')
      t = @create_token( TokenType.LITERAL_CHARACTER, text )
    else
      t = @create_token( TokenType.LITERAL_STRING, text )
    return t

  create_token: (type,value) ->
    t = new Token( type, @filepath, @line, @column )
    if (typeof(value) != "undefined") then t.value = value
    return t

  define_tokens: () ->
    if (TokenType.definitions)
      TokenType.keywords ?= {}
      TokenType.name_lookup ?= {}
      TokenType.id_lookup ?= {}
      TokenType.value_lookup ?= {}
      TokenType.illegal_keywords ?= {}
      TokenType.symbol_list = []
      for i of TokenType.definitions
        def = TokenType.definitions[i]
        for key,value of def
          switch key
            when "token_type"
              TokenType[value] = Number(i)
              TokenType.id_lookup[i] = value
              TokenType.name_lookup[i] = value

            when "keyword"
              name = "KEYWORD_" + value
              TokenType.id_lookup[i] = name
              TokenType[name] = Number(i)
              TokenType.keywords[value] = Number(i)
              TokenType.name_lookup[i] = value
              TokenType.value_lookup[value] = Number(i)

            when "illegal_keyword"
              TokenType.keywords[value.keyword] = TokenType.ILLEGAL_KEYWORD
              TokenType.illegal_keywords[value.keyword] = value.message

            else
              # Symbol definition
              TokenType[key] = Number(i)
              TokenType.name_lookup[i] = value
              TokenType.id_lookup[i] = key
              TokenType.symbol_list.push( value )
              TokenType.value_lookup[value] = Number(i)

    if (not TokenType.ILLEGAL_KEYWORD)
      throw new ParseError( "No definition for required token type ILLEGAL_KEYWORD in BardParser.define_tokens()." )

  error: (message) ->
    throw new ParseError( message, @filepath, @line, @column )

  next_is_hex_digit: () ->
    return @scanner.peek().hex_character_to_number(0) != -1

  tokenize: (@filepath,data) ->
    Tokenizer.define_tokens( TokenType )

    @symbol_scan_tree = new ScanNode()
    for symbol in TokenType.symbol_list
      @symbol_scan_tree.define( symbol, TokenType.value_lookup[symbol] )

    options = {}
    if (data) then options.data = data
    @scanner = new Scanner( @filepath, options )
    @tokens = []

    while (@scanner.has_another())
      t = @parse_token()
      if (t)
        @tokens.push( t )

    return @tokens

  parse_token: () ->
    @consume_white_space()
    @line = @scanner.line
    @column = @scanner.column

    ch = @scanner.peek()

    # EOL
    if (ch == '\n')
      @scanner.read()
      return @create_token( TokenType.EOL )

    # Identifier or Keyword
    if (ch.is_letter() or ch == '_')
      st = @tokenize_identifier()
      type = TokenType.keywords[ st ]
      if (type?)
        if (type != TokenType.ILLEGAL_KEYWORD)
          return @create_token( type )
        else
          throw @error( TokenType.illegal_keywords[st] )

       else
          t = @create_token( TokenType.IDENTIFIER )
          t.value = st
          return t

    else if (ch >= '0' and ch <= '9')
      switch (@scanner.peek(1))
        when 'b'
          return @tokenize_integer_with_base(2)
        when 'c'
          return @tokenize_integer_with_base(8)
        when 'x'
          return @tokenize_integer_with_base(16)
        else
          return @tokenize_number()

    else if (ch == '\'')
      return @tokenize_characters( '\'' )

    else if (ch == '"')
      return @tokenize_characters( '"' )

    else if (ch == '#')
      return @tokenize_comment()

    else if (ch == '@' and @scanner.peek(1) == '|')
      return @tokenize_verbatim_string()

    else
      # Symbols
      if (ch == '.')
        # Could be a decimal such as ".1".
        next = @scanner.peek(1)
        if (next >= '0' and next <= '9') then return @tokenize_number()

      t = @symbol_scan_tree.parse( @scanner )
      if (t)
        t.filepath = @filepath
        t.line = @line
        t.column = @column
        return t

      error_message = "Syntax error - unrecognized symbol "
      if (ch >= ' ' and ch.charCodeAt(0) != 127)
        error_message += "'" + ch + "'."
      else
        error_message += "(Unicode " + ch.charCodeAt(0) + ")."

      throw @error( error_message )

  scan_character: () ->
    if (not @scanner.has_another())
      throw @error( "Character expected." )

    ch = @scanner.peek()
    if (ch == '\n')
      throw @error( "Character expected; found End Of Line." )

    if (ch == '\\')
      @scanner.read()
      if (not @scanner.has_another())
        throw @error( "Escaped character expected; found End Of File." )

      if (@consume('b')) then return String.fromCharCode(8)
      if (@consume('f')) then return String.fromCharCode(12)
      if (@consume('n')) then return '\n'
      if (@consume('r')) then return '\r'
      if (@consume('t')) then return '\t'
      if (@consume('v')) then return String.fromCharCode(11)
      if (@consume('0')) then return '\0'
      if (@consume('/')) then return '/'
      if (@consume('\''))then return '\''
      if (@consume('\\'))then return '\\'
      if (@consume('"')) then return '"'
      if (@consume('x')) then return @scan_hex_value(2)
      if (@consume('u')) then return @scan_hex_value(4)
      throw @error( "Invalid escape sequence.  Supported: \\n \\r \\t \\0 \\/ \\' \\\\ \\\" \\" + "uXXXX \\" + "xXX." )

    value = @scanner.read().charCodeAt(0)
    if ((value & 0x80) != 0)
      # Handle UTF8 encoding
      ch2 = @scanner.read().charCodeAt(0)

      if ((value & 0x20) == 0)
        # %110xxxxx 10xxxxxx
        value = value & 0x1f
        ch2 = value & 0x3f
        return String.fromCharCode((value<<6) | ch2)

      else
        # %1110xxxx 10xxxxxx 10xxxxxx
        ch3 = @scanner.read().charCodeAt(0)
        value = value & 15
        ch2 = ch2 & 0x3f
        ch3 = ch3 & 0x3f
        return String.fromCharCode((value<<2) | (ch2<<6) | ch3)

    return String.fromCharCode( value )

  scan_hex_value: (digits) ->
    value = 0
    i = 1
    while (i <= digits)
      if (not @scanner.has_another())
        throw @error( digits + "-digit hex value expected; found end of file." )

      ch = @scanner.read()
      intval = ch.hex_character_to_number(0)
      if (intval == -1)
        error_buffer = ""
        error_buffer = "Invalid hex digit "
        code = ch.charCodeAt(0)
        if (code < 32 or code >= 127)
          error_buffer += code
        else
          error_buffer += "'" + ch + "'"
        error_buffer += '.'
        throw @error( error_buffer )

      value = (value<<4) + intval
      ++i

    return String.fromCharCode( value )

  scan_integer: () ->
    n = 0
    ch = @scanner.peek()
    while (ch >= '0' and ch <= '9')
      intval = @scanner.read().charCodeAt(0) - 48
      n = n * 10.0 + intval
      ch = @scanner.peek()

    return n

  tokenize_verbatim_string: () ->
    buffer = ""
    @scanner.read()
    @scanner.read()
    while (@scanner.has_another())
      ch = @scanner.peek()
      if (ch == '\n')
        eol_line = @scanner.line
        eol_column = @scanner.column
        @scanner.read()
        @scanner.consume_spaces()
        if (@consume('|'))
          buffer += ch
        else
          @tokens.push new Token( TokenType.LITERAL_STRING, @filepath, @line, @column, buffer )
          @tokens.push new Token( TokenType.EOL, @filepath, eol_line, eol_column )
          return null
      else
        buffer += @scanner.read()

    throw error( "End Of File reached while looking for end of verbatim string." )

  tokenize_comment: () ->
    buffer = ""
    @scanner.read()  # '#'
    if (@consume('{'))
      nesting_count = 1
      keep_reading = true
      while (@scanner.has_another())
        ch = @scanner.read()

        if (ch == '#')
          buffer += '#'
          if (@consume('{'))
            buffer += '{'
            ++nesting_count

        else if (ch == '}')
          if (@consume('#'))
            --nesting_count
            if (nesting_count == 0) then break
            else buffer += '}#'
          else
            buffer += '}'

        else
          buffer += ch
    else
      while (@scanner.has_another() and @scanner.peek() != '\n')
        buffer += @scanner.read()

    return @create_token( TokenType.COMMENT, buffer )

  tokenize_identifier: () ->
    result = ""
    ch = @scanner.peek()
    while (ch.is_letter() or ch.is_number() or ch == '_')
      result += @scanner.read()
      ch = @scanner.peek()

    return result

  tokenize_characters: (terminator) ->
    buffer = ""
    @scanner.read()  # left terminator
    while (@scanner.has_another())
      ch = @scanner.peek()
      if (ch == terminator)
        @scanner.read()
        return @convert_characters_to_token( terminator, buffer )
      else
        buffer += @scan_character()

    throw error( "End Of file reached while looking for end of string." )

  tokenize_integer_with_base: (base) ->
    @scanner.read()  # '0'
    @scanner.read()  # [b,c,x] = [2,8,16]

    bits_per_value = 1
    while ((1 << bits_per_value) < base)
      ++bits_per_value

    count = 0
    n = 0
    digit = @scanner.peek().hex_character_to_number()
    while (@scanner.has_another() and digit != -1)
      if (digit >= base)
        throw @error( "Digit out of range for base " + base + "." )
      ++count
      n = n * base + digit
      @scanner.read()
      digit = @scanner.peek().hex_character_to_number()  #TODO fix

    if (count == 0) then throw @error( "One or more digits expected." )

    return @create_token( TokenType.LITERAL_INTEGER, n )

  tokenize_number: () ->
    is_negative = @consume('-')
    is_real = false

    n  = @scan_integer()

    if (@scanner.peek() == '.')
      ch = @scanner.peek(1)
      if (ch >= '0' and ch <= '9')
        @scanner.read()
        is_real = true
        start_pos = @scanner.position
        fraction = @scan_integer()
        n += fraction / Math.pow( 10, (@scanner.position - start_pos) )

      else if (ch == '.')
        # Start of range
        if (is_negative) then n = -n
        return @create_token( TokenType.LITERAL_INTEGER, n )
      else
        if (is_negative) then n = -n
        return @create_token( TokenType.LITERAL_REAL, n )

    if (@consume('E') or @consume('e'))
      is_real = true
      negative_exponent = @consume('-')
      if (not negative_exponent) then @consume('+')
      power = @scan_integer()
      if (negative_exponent)
        n /= power
      else
        n *= power

    if (is_negative) then n = -n

    if (is_real)
      return @create_token( TokenType.LITERAL_REAL, n )
    else
      return @create_token( TokenType.LITERAL_INTEGER, n )

#==============================================================================
# Parser
#==============================================================================


#==============================================================================
# Exports
#==============================================================================
module.exports.ParseError  = ParseError
module.exports.Scanner = Scanner
module.exports.Token     = Token
module.exports.TokenType = TokenType
module.exports.Tokenizer = Tokenizer
