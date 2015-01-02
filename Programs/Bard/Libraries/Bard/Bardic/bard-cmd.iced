PROGRAM = require("./bard-program")
TOKENIZER = require("./bard-tokenizer")
Program = PROGRAM.Program
ParseError = TOKENIZER.ParseError

#==============================================================================
# Cmd
#==============================================================================
class Cmd
  cast: (from_type,to_type) ->
    if (from_type.instance_of(to_type))
      return this

    #if (from_type.is_primitive() and to_type.is_primitive)

    throw @error( "Cannot convert from existing type " + from_type.name + " to required type " + to_type.name + "." )

  cast_to: (to_type) ->
    return @cast( @type(), to_type )

  is_literal: () ->
    return false

  error: (message) ->
    return new ParseError( message )

  require_type: (cmd) ->
    type = cmd.type()
    if (type) then return type
    throw @t.error( "A value is required but a nil expression is given." )

  toString: () ->
    return "TODO"
    #throw @t.error( "toString() undefined for " + @constructor.name + "." )

  resolve: () ->
    return @traverse( "resolve" )

  traverse: (method_name) ->
    return this

  type: () ->
    if (@cached_type) then return @cached_type
    return null


class Literal extends Cmd
  is_literal: () ->
    return true

  toString: () ->
    return "" + @value + "->" + @type()

class LiteralNull extends Literal
  constructor: (@t) ->

  toString: () ->
    return "null"

class LiteralReal extends Literal
  constructor: (@t,@value) ->

  type: () ->
    return Program.type_Real

class LiteralInteger extends Literal
  constructor: (@t,@value) ->

  cast: (from_type,to_type) ->
    if (from_type.instance_of(to_type)) then return this

    switch (to_type)
      when Program.type_String
        return new LiteralString( @t, @value + "" )

      when Program.type_Real
        return new LiteralReal( @t, @value )

    return super(from_type,to_type)

    return super(from_type,to_type)

  type: () ->
    return Program.type_Integer

class LiteralCharacter extends Literal
  constructor: (@t,@value) ->

  cast: (from_type,to_type) ->
    if (from_type.instance_of(to_type)) then return this

    switch (to_type)
      when Program.type_Real
        return new LiteralReal( @t, @value.charCodeAt(0) )

      when Program.type_Integer
        return new LiteralInteger( @t, @value.charCodeAt(0) )

    return super(from_type,to_type)

  type: () ->
    return Program.type_Character

class LiteralLogical extends Literal
  constructor: (@t,@value) ->

  type: () ->
    return Program.type_Logical

class LiteralString extends Literal
  constructor: (@t,@value) ->

  cast: (from_type,to_type) ->
    if (from_type.instance_of(to_type)) then return this

    switch (to_type)
      when Program.type_Real
        return new LiteralReal( @t, Number(@value) )

      when Program.type_Integer
        return new LiteralInteger( @t, Number(@value) )

      when Program.type_Character
        if (@value == null or @value.length == 0)
          return new LiteralCharacter( @t, '\0' )
        return new LiteralCharacter( @t, @value[0] )

      #when Program.type_Byte
      #  if (@value == null or @value.length == 0)
      #    return new LiteralByte( @t, '\0' )
      #  return new LiteralByte( @t, @value[0] )

    return super(from_type,to_type)

  type: () ->
    return Program.type_String

class This
  constructor: (@t) ->

  toString: () ->
    return "this"

#==============================================================================
# Unary Commands
#==============================================================================
class Unary extends Cmd
  constructor: (@t,@value) ->

  resolve: () ->
    @value = @value.resolve()
    
    value_type = @require_type( @value )

    return @resolve_for_value_type( value_type )

  resolve_for_value_type: (value_type) ->
    return this

  toString: () ->
    return "(" + @value.toString() + @name() + ")"

  type: () ->
    return @value.type()

class UnaryWithModify extends Unary
  resolve: () ->
    @value = @value.resolve()
    
    # disable value requirement for new
    #value_type = @require_type( @value )
    # return @resolve_for_value_type( value_type )

    return this

class PostDecrement extends UnaryWithModify
  name: () ->
    return "--"

  resolve_for_value_type: (value_type) ->
    return this

class PostIncrement extends UnaryWithModify
  name: () ->
    return "++"

  resolve_for_value_type: (value_type) ->
    return this

class PreDecrement extends UnaryWithModify
  name: () ->
    return "--"

  resolve_for_value_type: (value_type) ->
    return this

  toString: () ->
    return "(--" + @value.toString() + ")"

class PreIncrement extends UnaryWithModify
  name: () ->
    return "++"

  resolve_for_value_type: (value_type) ->
    return this

  toString: () ->
    return "(++" + @value.toString() + ")"

class Logicalize extends Unary
  name: () ->
    return "?"

  resolve_for_value_type: (value_type) ->
    if (@value instanceof Literal)
      return new LiteralLogical( @value.t, !!@value.value )
    else
      return this

#==============================================================================
# Binary Commands
#==============================================================================
class Binary extends Cmd
  constructor: (@t,@lhs,@rhs) ->

  resolve: () ->
    @lhs = @lhs.resolve()
    @rhs = @rhs.resolve()
    
    lhs_type = @require_type( @lhs )
    rhs_type = @require_type( @rhs )

    return @resolve_args_of_type(lhs_type, rhs_type)

  cast_args_to_common_type: (lhs_type,rhs_type) ->
    if (lhs_type == rhs_type) then return lhs_type

    if (lhs_type.is_primitive and rhs_type.is_primitive)
      type = Program.type_String
      if (lhs_type == type or rhs_type == type)
        @lhs = @lhs.cast( lhs_type, type )
        @rhs = @rhs.cast( rhs_type, type )
        @cached_type = type
        return type

      type = Program.type_Real
      if (lhs_type == type or rhs_type == type)
        @lhs = @lhs.cast( lhs_type, type )
        @rhs = @rhs.cast( rhs_type, type )
        @cached_type = type
        return type

      type = Program.type_Integer
      if (lhs_type == type or rhs_type == type)
        @lhs = @lhs.cast( lhs_type, type )
        @rhs = @rhs.cast( rhs_type, type )
        @cached_type = type
        return type

      type = Program.type_Character
      if (lhs_type == type or rhs_type == type)
        @lhs = @lhs.cast( lhs_type, type )
        @rhs = @rhs.cast( rhs_type, type )
        @cached_type = type
        return type

    throw @error( "Cannot mix types " + lhs_type + " and " + rhs_type + " in this operation." )

  resolve_args_of_type: (lhs_type,rhs_type) ->
    console.log "TODO: resolve_args_of_type for " + @name()
    return this

  toString: () ->
    return @lhs.toString() + @name() + @rhs.toString()

class Add extends Binary
  name: () -> " + "

  resolve_args_of_type: (lhs_type,rhs_type) ->
    common_type = @cast_args_to_common_type( lhs_type, rhs_type )
    if (@lhs.is_literal() and @rhs.is_literal())
      switch (common_type)
        when Program.type_String
          return new LiteralString( @t, (@lhs.value + "" + @rhs.value) )

        when Program.type_Real
          return new LiteralReal( @t, @lhs.value + @rhs.value )

        when Program.type_Integer
          return new LiteralInteger( @t, (@lhs.value + @rhs.value) | 0 )

        when Program.type_Character
          return new LiteralString( @t, (@lhs.value + "" + @rhs.value) )

    throw @error( "Unable to resolve operation with argument types " + lhs_type + " and " + rhs_type + "." )


#==============================================================================
# Miscellaneous Expressions
#==============================================================================
class Access extends Cmd
  constructor: (@t,@name) ->

  toString: () ->
    return @name


#==============================================================================
# Assignment
#=============================================================================
class Assign extends Cmd
  constructor: (@t,@variable,@value) ->

  resolve: () ->
    @value    = @value.resolve()
    @variable = @variable.resolve()
    return this

  toString: () ->
    return @variable + " = " + @value


#==============================================================================
# Statements and Control Structures
#==============================================================================
class LocalDeclaration extends Cmd
  constructor: (@t,@name) ->
    if (value) then @value = value

  toString: () ->
    result = "local " + @name
    if (@initial_value) then result += "=" + @initial_value
    if (@type) then result += " : " + @type
    return result

class Return extends Cmd
  constructor: (@t,value) ->
    if (value) then @value = value

  resolve: () ->
    if (@value) then @value = @value.resolve()
    return this

  toString: () ->
    if (@value) then return "return " + @value
    return "return"


statements_to_string = (statements) ->
  result = ""
  for statement in statements
    result += statement.toString() + "\n"
  return result

class ControlStructure extends Cmd


class If extends ControlStructure
  constructor: (@t,@condition) ->
    @statements = []

  toString: () ->
    result = "if (" + @condition + ")\n"
    result += statements_to_string( @statements ).indent(2)

    if (@elseIf_conditions)
      for i of @elseIf_conditions
        result += "elseIf (" + @elseIf_conditions[i] + ")\n"
        result += statements_to_string( @elseIf_statements[i] ).indent(2)

    if (@else_statements)
      result += "else\n"
      result += statements_to_string( @else_statements ).indent(2)
    result += "endIf"
    return result

#==============================================================================
# EXPORTS
#=============================================================================
module.exports.statements_to_string = statements_to_string
module.exports.Access           = Access
module.exports.Add              = Add
module.exports.Assign           = Assign
module.exports.Binary           = Binary
module.exports.Cmd              = Cmd
module.exports.ControlStructure = ControlStructure
module.exports.If               = If
module.exports.Literal          = Literal
module.exports.LiteralCharacter = LiteralCharacter
module.exports.LiteralInteger   = LiteralInteger
module.exports.LiteralLogical   = LiteralLogical
module.exports.LiteralString    = LiteralString
module.exports.LiteralNull      = LiteralNull
module.exports.LiteralReal      = LiteralReal
module.exports.Logicalize       = Logicalize
module.exports.PostDecrement    = PostDecrement
module.exports.PostIncrement    = PostIncrement
module.exports.PreDecrement     = PreDecrement
module.exports.PreIncrement     = PreIncrement
module.exports.Return           = Return
module.exports.This             = This
module.exports.Unary            = Unary
module.exports.UnaryWithModify  = UnaryWithModify

