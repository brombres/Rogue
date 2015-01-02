#==============================================================================
# Attributes
#==============================================================================
Attributes =
  flag_format_mask: 7
  flag_primitive:   0
  flag_compound:    1
  flag_class:       2

#==============================================================================
# Template
#==============================================================================
class Template
  # Note: @tokens is assigned after Template is created.

  constructor: (@t,@name,@attributes) ->

  instantiate: (ref_t,ref_name) ->
    Parser = require("./bard-parser").Parser
    parser = new Parser( @tokens )

    type = new Type( @t, ref_name, @attributes )
    Program.types[ref_name] = type
    Program.type_list.push( type )

    parser.parse_type_def( type )
    return null

  is_class: () ->
    return !!(@attributes & Attributes.flag_class)


#==============================================================================
# Type
#==============================================================================
class Type
  constructor: (@t,@name,@attributes) ->
    @property_list = []
    @method_list = []

  instance_of: (base_type) ->
    return (this == base_type)

  is_primitive: () ->
    return (@attributes & Attributes.flag_format_mask) == Attributes.flag_primitive

  toString: () ->
    return @name


#==============================================================================
# Method
#==============================================================================
class Method
  constructor: (@t,@name) ->
    @statements = []

#==============================================================================
# Program
#==============================================================================
Program =
  reset: () ->
    @templates = {}
    @template_list = []

    @types = {}
    @type_list = []

    @statements = []

    @main_class_info = null

    Token = require("./bard-tokenizer").Token
    t = new Token( 0 )
    @type_Real      = @create_built_in_type( t, "Real", Attributes.flag_primitive )
    @type_Integer   = @create_built_in_type( t, "Integer", Attributes.flag_primitive )
    @type_Character = @create_built_in_type( t, "Character", Attributes.flag_primitive )
    @type_Logical   = @create_built_in_type( t, "Logical", Attributes.flag_primitive )
    @type_String    = @create_built_in_type( t, "String", Attributes.flag_primitive )

  create_built_in_type: (t,name,attributes) ->
    type = new Type(t,name,attributes)
    @types[name] = type
    @type_list.push( type )
    return type

  reference_type: (t,name) ->
    type = @types[name]
    if (type) then return type

    base_name = name.before_first( '<' )

    template = @templates[base_name]
    if (not template)
      throw t.error "Reference to undefined type '#{name}'."

    return template.instantiate(t,name)

  resolve: () ->
    if (Program.main_class_info)
      Program.main_class_info.type = Program.reference_type( Program.main_class_info.t, Program.main_class_info.name )

    for statement,index in @statements
      @statements[index] = @visit( statement, "resolve" )

    for statement,index in @statements
      console.log index + ": " + statement.toString()

  visit: (cmd,method_name) ->
    if (not cmd) then return null

    if (cmd[method_name])
      result = cmd[method_name]()
      return result
    else
      return cmd.traverse( method_name )


#==============================================================================
# EXPORTS
#==============================================================================
module.exports.Attributes = Attributes
module.exports.Method     = Method
module.exports.Program    = Program
module.exports.Type       = Type
module.exports.Template   = Template

