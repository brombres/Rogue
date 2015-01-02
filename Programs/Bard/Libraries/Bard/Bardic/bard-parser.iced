file = require("file")
require("./bard-string-augment")
TOKENIZER = require("./bard-tokenizer")
PROGRAM  = require("./bard-program")
ParseError = TOKENIZER.ParseError
Token      = TOKENIZER.Token
Tokenizer  = TOKENIZER.Tokenizer
TokenType  = TOKENIZER.TokenType
Attributes = PROGRAM.Attributes
Method     = PROGRAM.Method
Program    = PROGRAM.Program
Template   = PROGRAM.Template
Type       = PROGRAM.Type

CMD = require( "./bard-cmd" )

#==============================================================================
# TokenReader
#==============================================================================
class TokenReader
  constructor: (@tokens) ->
    @position = 0
    @count = @tokens.length

  has_another: () ->
    return (@position < @count)

  peek: ( num_ahead=0 ) ->
    i = @position + num_ahead
    if (i < @count) then return @tokens[i]
    return null

  read: () ->
    return @tokens[ @position++ ]

  read_identifier: () ->
    t = @read()
    if (t.type != TokenType.IDENTIFIER)
      throw t.error( "Identifier expected." )
    return t.value


#==============================================================================
# Preprocessor
#==============================================================================
Preprocessor =
  process: (tokens) ->
    new_tokens = []

    previous_eol = null
    last_is_eol = false
    in_comment = false

    # Insert comment text as values of EOL tokens to support
    # auto-documentation.
    for t in tokens
      switch t.type
        when TokenType.EOL
          if (last_is_eol)
            # Two EOL's breaks comment appending.
            in_comment = false

          last_is_eol = true
          if (not in_comment)
            previous_eol = t
            new_tokens.push( t )

        when TokenType.COMMENT
          last_is_eol = false
          if (previous_eol)
            in_comment = true
            if (previous_eol.value)
              previous_eol.value += "\n" + t.value
            else
              previous_eol.value = t.value

          else if (t.line == 1)
            # If there is no previous EOL *and* the line number of the 
            # comment is 1, turn the comment into an EOL.  Otherwise we just
            # discard it.
            last_is_eol = true
            in_comment = true
            t.type = TokenType.EOL
            new_tokens.push( t )
            previous_eol = t

        else
          last_is_eol = false
          in_comment = false
          previous_eol = null
          new_tokens.push( t )


    # Merge literal strings that are either consecutive or separated by
    # exactly one EOL.
    tokens = new_tokens
    new_tokens = []

    for t in tokens
      if (t.type == TokenType.LITERAL_STRING)
        count = new_tokens.length
        if (count > 0)
          if (new_tokens[count-1].type == TokenType.LITERAL_STRING)
            new_tokens[count-1].value += t.value
            continue

          if (count > 1 and
              new_tokens[count-2].type == TokenType.LITERAL_STRING and
              new_tokens[count-1].type == TokenType.EOL)
            new_tokens[count-2].value += t.value
            continue

      new_tokens.push( t )

    #console.log new_tokens

    return new_tokens


#==============================================================================
# Parser
#==============================================================================
class Parser
  constructor: (filepath,data) ->
    @pending_files = []
    @processed_filepaths = {}

    if (filepath)
      if (!filepath.length or filepath[0] instanceof Token)
        # 'filepath' is actually a token list
        tokens = filepath
        @reader = new TokenReader( tokens )
      else
        @include( filepath, data )

  include: (filepath,data) ->
    filepath = file.path.abspath( filepath )

    if (@processed_filepaths[filepath]) then return  # already processed
    @processed_filepaths[filepath] = filepath

    @pending_files.push( {filepath:filepath,data:data} )

  parse_program: () ->
    Program.reset()

    while (@tokenize_next_file())
      while (@parse_element()) then

    return Program

  tokenize_next_file: ()->
    if (@pending_files.length == 0) then return false

    file = @pending_files.shift()

    tokens = Tokenizer.tokenize( file.filepath, file.data )
    tokens = Preprocessor.process( tokens )
    @reader = new TokenReader( tokens )

    return true

  consume: (token_type) ->
    t = @reader.peek()
    if (not t or t.type != token_type) then return false
    @reader.read()
    return true

  consume_end_commands: () ->
    found = false
    while (@consume(TokenType.EOL) or @consume(TokenType.SYMBOL_SEMICOLON))
      found = true
    return found

  consume_eols: () ->
    found_any = false
    while (@consume(TokenType.EOL))
      found_any = true
    return found_any

  must_consume: (token_type,message) ->
    t = @peek()
    if (@consume(token_type)) then return t

    if (not message)
      message = "'" + TokenType.name_lookup[ token_type ] + "' expected but found \"#{t}\"."

    if (not @reader.peek())
      throw new ParseError( "End of file; " + message )

    throw @peek().error( "Syntax error: " + message )

  next_is: (type) ->
    if (not @reader.has_another()) then return false
    return (@reader.peek().type == type)

  next_is_end_command: () ->
    if (not @reader.has_another()) then return false
    type = @reader.peek().type
    return (type == TokenType.EOL or type == TokenType.SYMBOL_SEMICOLON)

  next_is_statement: () ->
    if (not @reader.has_another()) then return false
    return (@reader.peek().type > TokenType.LAST_NON_STATEMENT)

  parse_element: () ->
    @consume_end_commands()
    t = @reader.peek()

    if (@consume(TokenType.KEYWORD_class))
      @parse_template_def( t, Attributes.flag_class, TokenType.KEYWORD_endClass )
      return true

    if (not @reader.has_another()) then return false

    if (@next_is_statement())
      @parse_multi_line_statements( Program.statements )
    else
      @parse_expression()  # Will generate an error

    return true

  parse_template_def: (t,attributes,closing_type) ->
    template = new Template( t, @reader.read_identifier(), attributes )

    if (@consume(TokenType.SYMBOL_OPEN_SPECIALIZE))
      template.placeholder_info = []
      first = true
      while (first or @consume(TokenType.SYMBOL_COMMA))
        first = false
        t = @must_consume( TokenType.SYMBOL_DOLLAR, "'$Placeholder' expected." )
        name = @reader.read_identifier()
        template.placeholder_info.push( {t:t,name:name} )

      @must_consume( TokenType.SYMBOL_CLOSE_SPECIALIZE, "'$Placeholder' or '>>' expected." )
      #console.log template.placeholder_info

    else if (not Program.main_class_info and template.is_class())
      Program.main_class_info = {t:t,name:template.name}

    tokens = []
    template.tokens = tokens
    Program.template_list.push( template )
    Program.templates[template.name] = template

    if (@consume(TokenType.SYMBOL_SEMICOLON)) then return
    @must_consume( TokenType.EOL )

    while (@reader.has_another())
      t = @reader.read()
      if (t.type == closing_type)
        return

      tokens.push( t )

    throw t.error( "Type definition is missing closing '" + TokenType.name_lookup[closing_type] + "'." )

  parse_type_def: (@this_type) ->
    while (@parse_category()) then

    if (@reader.has_another())
      t = @reader.peek()
      throw t.error "Syntax error: unexpected \"#{t}\"."

  parse_category: () ->
    @consume_end_commands()

    if (@consume(TokenType.KEYWORD_PROPERTIES))
      while (@parse_property_list()) then
      return true

    if (@consume(TokenType.KEYWORD_METHODS))
      while (@parse_method()) then
      return true

    return false

  parse_property_list: () ->
    @consume_end_commands()
    if (not @next_is(TokenType.IDENTIFIER)) then return false

    t = @peek()

    name = @reader.read_identifier()

    initial_value = undefined
    if (@consume(TokenType.SYMBOL_EQUALS))
      initial_value = @parse_expression()

    p = {t:t, name:name, initial_value:initial_value}
    @this_type.property_list.push( p )

    return true

  parse_method: () ->
    @consume_end_commands()
    t = @peek()

    if (not @consume(TokenType.KEYWORD_method)) then return false

    name = @reader.read_identifier()

    m = new Method( t, name )
    @this_type.method_list.push( m )

    if (@consume(TokenType.SYMBOL_ARROW))
      m.return_type = @parse_type()

    @parse_multi_line_statements( m.statements )

    return true

  parse_type: () ->
    t = @peek()
    name = @reader.read_identifier()
    return Program.reference_type( t, name )

  parse_multi_line_statements: (statements) ->
    @consume_end_commands()
    while (@next_is_statement())
      @parse_statement( statements, true )
      while (@consume(TokenType.EOL) or @consume(TokenType.SYMBOL_SEMICOLON)) then # no action
      @consume_end_commands()

  parse_single_line_statements: (statements) ->
    while (@next_is_statement())
      @parse_statement( statements, false )
      if (not @consume(TokenType.SYMBOL_SEMICOLON)) then return
      while (@consume(TokenType.SYMBOL_SEMICOLON)) then # noAction

    if (not @consume(TokenType.EOL))
      if (@reader.peek().type >= TokenType.LAST_NON_STATEMENT) then @must_consume( TokenType.EOL )  # force an error

  parse_statement: (statements,allow_control_structures) ->
    t = @peek()

    #if (next_is(TokenType.keyword_delegate))
    #  throw t.error( "Unused delegate definition." )
    #endIf

    if (allow_control_structures)
      if (@next_is(TokenType.KEYWORD_if))
        statements.push( @parse_if() )
        return

    #  elseIf (next_is(TokenType.keyword_which))
    #    statements.push( parse_which )
    #    return

    #  elseIf (next_is(TokenType.keyword_whichIs))
    #    statements.push( parse_which( true ) )
    #    return

    #  elseIf (next_is(TokenType.keyword_forEach))
    #    statements.push( parse_forEach )
    #    return

    #  elseIf (next_is(TokenType.keyword_contingent))
    #    statements.push( parse_contingent )
    #    return

    #  elseIf (next_is(TokenType.keyword_while))
    #    statements.push( parse_while )
    #    return

    #  elseIf (next_is(TokenType.keyword_loop))
    #    statements.push( parse_loop )
    #    return

    #  elseIf (next_is(TokenType.keyword_try))
    #    statements.push( parse_try )
    #    return

    #  endIf
    else
      err = false
      if (@next_is(TokenType.KEYWORD_if)) then err = true

      if (err) then throw t.error( "Control structures must begin on a separate line." )

    if (@consume(TokenType.KEYWORD_return))
      if (@next_is_end_command())
        statements.push( new CMD.Return(t) )
      else
        statements.push( new CMD.Return(t,@parse_expression()) )

      return

    #else f (next_is(TokenType.keyword_local))
    #  parse_local_declaration( statements )
    #  return

    #elseIf (consume(TokenType.keyword_yield))

    #  if (next_is_end_command)
    #    statements.push( CmdYieldNil(t) )
    #  elseIf (this_method.is_task and this_method.return_type is null)
    #    throw t.error( "This task does not declare a return type." )
    #  else
    #    statements.push( CmdYieldValue(t,parse_expression) )
    #  endIf
    #  return

    #elseIf (consume(TokenType.keyword_yieldAndWait))
    #  statements.push( CmdYieldAndWait(t,parse_expression) )
    #  return

    #elseIf (consume(TokenType.keyword_yieldWhile))
    #  local cmd_while = CmdWhile( t, parse_expression )

    #  if (consume(TokenType.keyword_withTimeout))
    #    local t2 = peek
    #    local timeout_expr = parse_expression
    #    local timeout_name = "_timeout_" + Program.unique_id
    #    cmd_while.condition = CmdLogicalAnd( t2,
    #      CmdCompareLT( t2, CmdAccess(t2, "Time", "current"), CmdAccess(t2,timeout_name) ),
    #      cmd_while.condition )

    #    local v = Local( t2, timeout_name )
    #    this_method.add_local( v )
    #    statements.push( CmdLocalDeclaration( t2, v ) )
    #    statements.push( 
    #      CmdWriteLocal( t2, v, 
    #        CmdAdd( t2,
    #          CmdAccess(t2,"Time","current"),
    #          timeout_expr
    #        )
    #      )
    #    )
    #  endIf

    #  cmd_while.body.add( CmdYieldNil(t) )
    #  statements.push( cmd_while )

    #  return

    #elseIf (consume(TokenType.keyword_throw))
    #  statements.push( CmdThrow(t,parse_expression) )
    #  return

    #elseIf (consume(TokenType.keyword_noAction))
    #  noAction
    #  return

    #elseIf (consume(TokenType.keyword_trace))
    #  local cmd_trace = CmdTrace(t,this_method)
    #  while (reader.has_another)
    #    if (next_is(TokenType.symbol_semicolon) or next_is(TokenType.eol) or next_is(TokenType.symbol_close_brace))
    #      escapeWhile
    #    endIf

    #    local pos1 = reader.position
    #    t = peek
    #    if (consume(TokenType.symbol_comma))
    #      cmd_trace.labels.add(", ")
    #      cmd_trace.expressions.add( CmdLiteralString(t,", ") )
    #    else
    #      cmd_trace.expressions.add( parse_expression )
    #      cmd_trace.labels.add( reader.source_string(pos1,reader.position-1) )
    #    endIf

    #  endWhile
    #  statements.push( cmd_trace )
    #  return

    #elseIf (consume(TokenType.keyword_tron))
    #  statements.push( CmdTron(t) )
    #  return

    #elseIf (consume(TokenType.keyword_troff))
    #  statements.push( CmdTroff(t) )
    #  return
    #endIf

    ##{
    #if (consume("println"))
    #  if (next_is_end_command) statements.push( CmdPrintln(t) )
    #  else                     statements.push( CmdPrintln(t, parse_expression) )
    #  return
    #endIf

    #if (consume("print"))
    #  statements.push( CmdPrintln(t, parse_expression).without_newline )
    #  return
    #endIf
    #}#

    #if (consume(TokenType.keyword_escapeContingent))
    #  statements.push( CmdEscapeContingent(t) )
    #  return
    #endIf

    #if (consume(TokenType.keyword_escapeForEach))
    #  statements.push( CmdEscapeForEach(t) )
    #  return
    #endIf

    #if (consume(TokenType.keyword_escapeIf))
    #  statements.push( CmdEscapeIf(t) )
    #  return
    #endIf

    #if (consume(TokenType.keyword_escapeLoop))
    #  statements.push( CmdEscapeLoop(t) )
    #  return
    #endIf

    #if (consume(TokenType.keyword_escapeTry))
    #  statements.push( CmdEscapeTry(t) )
    #  return
    #endIf

    #if (consume(TokenType.keyword_escapeWhich))
    #  statements.push( CmdEscapeWhich(t) )
    #  return
    #endIf

    #if (consume(TokenType.keyword_escapeWhile))
    #  statements.push( CmdEscapeWhile(t) )
    #  return
    #endIf

    #if (consume(TokenType.keyword_nextIteration))
    #  statements.push( CmdNextIteration(t) )
    #  return
    #endIf

    #if (consume(TokenType.keyword_necessary))
    #  statements.push( CmdNecessary(t, parse_expression))
    #  return
    #endIf

    #if (consume(TokenType.keyword_sufficient))
    #  statements.push( CmdSufficient(t, parse_expression))
    #  return
    #endIf

    #if (consume(TokenType.symbol_increment))
    #  statements.push( CmdIncrement(t, parse_expression) )
    #  return
    #endIf

    #if (consume(TokenType.symbol_decrement))
    #  statements.push( CmdDecrement(t, parse_expression) )
    #  return
    #endIf

    expression = @parse_expression()

    t = @reader.peek()
    if (@consume(TokenType.SYMBOL_EQUALS))
      statements.push( new CMD.Assign(t,expression,@parse_expression()) )
      return

    ##{
    #if (consume(TokenType.symbol_access_assign))
    #  local rhs = parse_expression
    #  local access = rhs as CmdAccess
    #  if (access is null) throw rhs.error( "Property access or method call expected." )
    #  while (access.operand?)
    #    local operand = access.operand as CmdAccess
    #    if (operand is null) throw access.operand.t.error( "Property access or method call expected." )
    #    access = operand
    #  endWhile
    #  access.operand = expression.clone
    #  statements.push( CmdAssign(t,expression,access) )
    #endIf
    #}#

    #if (consume(TokenType.symbol_increment))
    #  statements.push( CmdIncrement(t, expression) )
    #  return
    #endIf

    #if (consume(TokenType.symbol_decrement))
    #  statements.push( CmdDecrement(t, expression) )
    #  return
    #endIf

    #local t_type = t.type
    #if (t_type >= TokenType.first_shorthand_operator and t_type <= TokenType.last_shorthand_operator)
    #  read
    #  local lhs = expression.clone
    #  local rhs = parse_expression
    #  local new_value : Cmd

    #  which (t_type)
    #    case TokenType.symbol_add_assign:          new_value = CmdAdd( t, lhs, rhs )
    #    case TokenType.symbol_subtract_assign:     new_value = CmdSubtract( t, lhs, rhs )
    #    case TokenType.symbol_multiply_assign:     new_value = CmdMultiply( t, lhs, rhs )
    #    case TokenType.symbol_divide_assign:       new_value = CmdDivide( t, lhs, rhs )
    #    case TokenType.symbol_mod_assign:          new_value = CmdMod( t, lhs, rhs )
    #    case TokenType.symbol_power_assign:        new_value = CmdPower( t, lhs, rhs )
    #    case TokenType.symbol_bitwise_and_assign:  new_value = CmdBitwiseAnd( t, lhs, rhs )
    #    case TokenType.symbol_bitwise_or_assign:   new_value = CmdBitwiseOr( t, lhs, rhs )
    #    case TokenType.symbol_bitwise_xor_assign:  new_value = CmdBitwiseXor( t, lhs, rhs )
    #    case TokenType.symbol_access_assign
    #      local access = rhs as CmdAccess
    #      if (access is null or access.operand?) throw t.error( "Syntax error." )
    #      access.operand = lhs
    #      new_value = access
    #  endWhich
    #  statements.push( CmdAssign(t, expression, new_value) )
    #  return
    #endIf

    ## No-parens args can follow an initial expression
    #if (not next_is_end_command)
    #  local access = expression as CmdAccess
    #  if (access? and access.args is null)
    #    local args = CmdArgs()
    #    while (not next_is_end_command and peek.type >= TokenType.last_non_statement)
    #      args.add( parse_expression )
    #    endWhile
    #    access.args = args
    #  endIf
    #endIf

    statements.push( expression )

  parse_if: () ->
    t = @reader.read()
    cmd_if = new CMD.If( t, @parse_expression() )

    if (@consume_eols())
      # multi-line if
      @parse_multi_line_statements( cmd_if.statements )

      while (@next_is(TokenType.KEYWORD_elseIf))
        if (not cmd_if.elseIf_conditions)
          cmd_if.elseIf_conditions = []
          cmd_if.elseIf_statements = []

        # Need a slight kludge to avoid dangling elseIf problems.
        starting_position = @reader.position
        t = @reader.read()
        elseIf_condition = @parse_expression()
        if (@consume(TokenType.EOL))
          elseIf_statements = []
          @parse_multi_line_statements( elseIf_statements )

          cmd_if.elseIf_conditions.push( elseIf_condition )
          cmd_if.elseIf_statements.push( elseIf_statements )
        else
          @reader.position = starting_position
          break

      if (@next_is(TokenType.KEYWORD_else) and @reader.peek(1).type == TokenType.EOL)
        @reader.read()
        cmd_if.else_statements = []
        @parse_multi_line_statements( cmd_if.else_statements )

      @must_consume( TokenType.KEYWORD_endIf )

    else
      # single-line if
      @parse_single_line_statements( cmd_if.statements )
      @consume_eols()

      while (@next_is(TokenType.KEYWORD_elseIf))
        if (not cmd_if.elseIf_conditions)
          cmd_if.elseIf_conditions = []
          cmd_if.elseIf_statements = []

        # Need logic to avoid dangling elseIf problems.
        starting_position = @reader.position
        t = @reader.read()
        elseIf_condition = @parse_expression()
        if (@next_is(TokenType.EOL))
          @reader.position = starting_position
          break
        else
          elseIf_statements = []
          @parse_single_line_statements( elseIf_statements )
          @must_consume( TokenType.EOL )

          cmd_if.elseIf_conditions.push( elseIf_condition )
          cmd_if.elseIf_statements.push( elseIf_statements )

      if (@next_is(TokenType.KEYWORD_else) and @reader.peek(1).type != TokenType.EOL)
        @reader.read()
        cmd_if.else_statements = []
        @parse_single_line_statements( cmd_if.else_statements )

    return cmd_if


  #----------------------------------------------------------------------------
  parse_expression: () ->
    return @parse_add_subtract()


  parse_add_subtract: (lhs) ->
    if (lhs)
      t = @peek()
      if (@consume(TokenType.SYMBOL_PLUS))
        @consume_eols()
        return @parse_add_subtract( new CMD.Add(t,lhs,@parse_multiply_divide_mod()) )
      #else if (@consume(TokenType.SYMBOL_MINUS))
      #  @consume_eols()
      #  return @parse_add_subtract( CmdSubtract(t,lhs,parse_multiply_divide_mod) )
      return lhs

    else
      return @parse_add_subtract( @parse_multiply_divide_mod() )

  parse_multiply_divide_mod: () ->
    return @parse_pre_unary()

  parse_pre_unary: () ->
    t = @reader.peek()
    if (@consume(TokenType.SYMBOL_INCREMENT))
      return new CMD.PreIncrement( t, @parse_pre_unary() )
    else if (@consume(TokenType.SYMBOL_DECREMENT))
      return new CMD.PreDecrement( t, @parse_pre_unary() )

    #else if (@consume(TokenType.KEYWORD_not))
    #  @consume_eols()
    #  return new CMD.LogicalNot( t, parse_pre_unary )
    #else if (@consume(TokenType.SYMBOL_MINUS))
    #  @consume_eols()
    #  return new CMD.Negate( t, parse_pre_unary )
    #else if (@consume(TokenType.SYMBOL_BITWISE_NOT))
    #  @consume_eols()
    #  return new CMD.BitwiseNot( t, parse_pre_unary )

    return @parse_post_unary()

  parse_post_unary: (operand) ->
    if (operand)
      t = @peek()
      if (@consume(TokenType.SYMBOL_QUESTION_MARK))
        return @parse_post_unary( new CMD.Logicalize(t, operand) )
      else if (@consume(TokenType.SYMBOL_INCREMENT))
        return @parse_post_unary( new CMD.PostIncrement( t, operand ) )
      else if (@consume(TokenType.SYMBOL_DECREMENT))
        return @parse_post_unary( new CMD.PostDecrement( t, operand ) )
      else
        return operand
    else
      return @parse_post_unary( @parse_term() )

  parse_term: () ->
    t = @peek()
    if (@consume(TokenType.SYMBOL_OPEN_PAREN))
      @consume_eols()
      result = @parse_expression()
      @consume_eols()
      @must_consume( TokenType.SYMBOL_CLOSE_PAREN )
      return result

    else if (@next_is(TokenType.IDENTIFIER) or @next_is(TokenType.SYMBOL_AT))
      return @parse_access_command( t )

    else if (@consume(TokenType.LITERAL_STRING))
    #  if (@consume(TokenType.SYMBOL_OPEN_PAREN))
    #    local first = true
    #    local args = CmdArgs()
    #    while (first or @consume(TokenType.symbol_comma))
    #      first = false
    #      args.add( @parse_expression() )
    #    endWhile
    #    @must_consume( TokenType.symbol_close_paren )
    #    return CmdFormattedString( t, t->String, args )
    #  endIf
      return new CMD.LiteralString( t, t.value )

    else if (@consume(TokenType.KEYWORD_null))
      return new CMD.LiteralNull(t)

    else if (@consume(TokenType.LITERAL_REAL))
      return new CMD.LiteralReal(t, t.value)

    else if (@consume(TokenType.LITERAL_INTEGER))
      return new CMD.LiteralInteger(t, t.value)

    else if (@consume(TokenType.LITERAL_CHARACTER))
      return new CMD.LiteralCharacter(t, t.value)

    else if (@consume(TokenType.KEYWORD_true))
      return new CMD.LiteralLogical( t, true )

    else if (@consume(TokenType.KEYWORD_false))
      return new CMD.LiteralLogical( t, false )

    else if (@consume(TokenType.KEYWORD_this))
      return new CMD.This( t ) #, this_type )

    #else if (@consume(TokenType.keyword_function))
    #  return parse_function( t )

    else if (@consume(TokenType.KEYWORD_pi))
      return new CMD.LiteralReal( t, Math.PI )

    #else if (@consume(TokenType.symbol_open_bracket))
    #  # [ literal, list ]
    #  if (parsing_properties?)
    #    local cmd = CmdNewObject( t, Types.reference(t,"PropertyList") ) : Cmd

    #    local first = true
    #    while (first or @consume(TokenType.symbol_comma))
    #      first = false
    #      local value = @parse_expression()
    #      cmd = CmdAccess( value.t, cmd, "add", CmdArgs(value) )
    #      @consume_eols()
    #    endWhile
    #    @must_consume( TokenType.symbol_close_bracket )

    #    return cmd

    #  else
    #    local list = CmdLiteralList(t)
    #    @consume_eols()
    #    if (not @consume(TokenType.symbol_close_bracket))
    #      local first = true
    #      while (first or @consume(TokenType.symbol_comma))
    #        first = false
    #        list.args.add( @parse_expression() )
    #        @consume_eols()
    #      endWhile
    #      @must_consume( TokenType.symbol_close_bracket )
    #    endIf
    #    return list
    #  endIf

    #else if (@consume(TokenType.symbol_open_brace))
    #  # { key:value, key:value, ... }
    #  local table = CmdLiteralTable(t)
    #  @consume_eols()
    #  if (not @consume(TokenType.symbol_close_brace))
    #    local first = true
    #    while (first or @consume(TokenType.symbol_comma))
    #      first = false
    #      if (@peek().type == TokenType.identifier)
    #        local kt = read
    #        table.keys.add( CmdLiteralTableKey(kt, kt->String) )
    #      else
    #        ++parsing_properties
    #        table.keys.add( @parse_expression() )
    #        --parsing_properties
    #      endIf
    #      @must_consume( TokenType.symbol_colon )
    #      ++parsing_properties
    #      table.values.add( @parse_expression() )
    #      --parsing_properties
    #      @consume_eols()
    #    endWhile
    #    @must_consume( TokenType.symbol_close_brace )
    #  endIf
    #  return table

    #else if (@consume(TokenType.symbol_empty_braces))
    #  return CmdNewObject( t, Types.reference( t, "PropertyTable" ) )

    #else if (@consume(TokenType.symbol_empty_brackets))
    #  return CmdNewObject( t, Types.reference( t, "PropertyList" ) )

    #else if (@consume(TokenType.keyword_prior))
    #  @consume_eols()
    #  @must_consume( TokenType.symbol_period )
    #  @consume_eols()
    #  local name = read_identifier
    #  local args = parse_args
    #  return CmdPriorCall( t, name, args )

    #else if (next_is(TokenType.keyword_delegate))
    #  return parse_delegate

    #else if (@consume(TokenType.alias_arg_index))
    #  return alias_args[ t->Integer ].clone

    else
      throw @peek().error( "Syntax error: unexpected \"" + @peek() + "\"." )

  parse_access_command: (t) ->
    name = @reader.read_identifier()
    result = new CMD.Access( t, name )
    return result

  peek: ()->
    return @reader.peek()



#==============================================================================
# EXPORTS
#==============================================================================
module.exports.Parser       = Parser
module.exports.Preprocessor = Preprocessor
module.exports.Program      = Program
module.exports.TokenType    = Tokenizer.TokenType

