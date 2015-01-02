// $[prefix ADC3]

// $[inline]
// Array<<Object>>::resized(Integer) -> ADC3_array_resized( $0, $1 )
// Array<<String>>::resized(Integer) -> ADC3_array_resized( $0, $1 )
// ListType::to_PropertyList()  -> $0.data
// Math::acos(Real)             -> Math.acos($1)
// Math::floor(Real)            -> Math.floor($1)
// Math::sqrt(Real)             -> Math.sqrt($1)
// Object::type_name()          -> $0.type_name
// Object[]::get(Integer)       -> $0.data[$1]
// Properties::put(String,Integer)     -> ADC3Properties__put__String_Properties($0,$1,$2)
// Properties::put(String,Logical)     -> ADC3Properties__put__String_Properties($0,$1,$2)
// Properties::put(String,Real)        -> ADC3Properties__put__String_Properties($0,$1,$2)
// Properties::put(String,String)      -> ADC3Properties__put__String_Properties($0,$1,$2)
// String::equals(String)     -> ($0 == $1)
// StringBuilder::indent()    -> $0.indent
// Time::current()            -> (Date.now() / 1000.0)

// $[code]
function ADC3Test__native_js( context )
{
  console.log( "Native test successful." );
}

function ADC3_array_resized( array, new_size )
{
  // Needs to be a function for call chaining
  array.length = new_size;
  return array;
}

function ADC3_mod_real( a, b )
{
  var q = a / b;
  return a - (Math.floor(q) * b);
}

function ADC3_mod_integer( a, b )
{
  if (a == 0 || b == 0) return 0;

  if (b == 1) return 0;

  if ((a ^ b) < 0)
  {
    var r = a % b;
    return (!!r) ? (r+b) : r;
  }
  else
  {
    return a % b;
  }
}

function ADC3_throw_missing_return()
{
  alert( "Missing 'return'." );
}

function ADC3Object__create_instance()
{
  alert( "TODO: ADC3Object__create_instance() " );
}

//=============================================================================
//  Console
//=============================================================================
var ADC3Console_string_buffer = "";

function ADC3Console__write__Character( THIS, local_value )
{
  if (local_value == 10)
  {
    window.console.log( ADC3Console_string_buffer );
    ADC3Console_string_buffer = "";
  }
  else
  {
    ADC3Console_string_buffer += String.fromCharCode( local_value );
  }
}

function ADC3Console__write__String( THIS, local_value )
{
  ADC3Console_string_buffer += local_value;
  window.console.log( ADC3Console_string_buffer );
  ADC3Console_string_buffer = "";
}


//=============================================================================
//  EventQueue
//=============================================================================
function ADC3EventQueue()
{
  this.events = [];
  this.args   = [];
}

function ADC3EventQueue__create( THIS )
{
  return new ADC3EventQueue();
}

function ADC3EventQueue__advance( THIS )
{
  if (THIS.events.length) THIS.args = THIS.events.shift();
  else                    THIS.args = [];
}

function ADC3EventQueue__clear( THIS )
{
  THIS.events = [];
  THIS.args = [];
  return THIS;
}

function ADC3EventQueue__count( THIS )
{
  return THIS.args.length;
}

function ADC3EventQueue__has_another( THIS )
{
  if (THIS.args.length) return true;

  if (THIS.events.length) 
  {
    THIS.args = THIS.events.shift();
    return !!THIS.args.length;
  }
  else
  {
    return false;
  }
}

function ADC3EventQueue__read_string( THIS )
{
  if (THIS.args.length) return THIS.args.shift();
  else                  return "";
}

function ADC3EventQueue__native_trace__Object( THIS )
{
  // No action
}

//=============================================================================
//  GenericArray
//=============================================================================
function ADC3GenericArray__clear_references__Integer_Integer( THIS, i1, i2 )
{
  if (THIS.type && THIS.type.name == "Array<<Object>>")
  {
    for (var i=i1; i<=i2; ++i) THIS[i] = null;
  }
}

function ADC3GenericArray__shift__Integer_Integer_Integer( THIS, i1, i2, delta )
{
  var endex = THIS.length - 1;
  if (i1 < 0) i1 = 0;
  else if (i1 > endex) i1 = endex;
  if (i2 < 0) i2 = 0;
  else if (i2 > endex) i2 = endex;

  var j1 = i1 + delta;
  var j2 = i2 + delta;
  if (j1 < 0) j1 = 0;
  else if (j1 > endex) j1 = endex;
  if (j2 < 0) j2 = 0;
  else if (j2 > endex) j2 = endex;

  if (j1 > j2) return;

  i1 = j1 - delta;
  i2 = j2 - delta;

  var count = (j2 - j1) + 1;

  if (delta > 0)
  {
    ++j2;
    ++i2;
    while (--count >= 0)
    {
      THIS[--j2] = THIS[--i2];
    }
  }
  else if (delta < 0)
  {
    --j1;
    --i1;
    while (--count >= 0)
    {
      THIS[++j1] = THIS[++i1];
    }
  }
}


//=============================================================================
//  JSON
//=============================================================================
function ADC3JSON__parse_list__String( THIS, local_st )
{
  return JSON.parse( local_st );
}

function ADC3JSON__parse_table__String( THIS, local_st )
{
  return JSON.parse( local_st );
}


//=============================================================================
//  Object
//=============================================================================
function ADC3Object__type_name( THIS )
{
  return THIS.type_name;
}

//=============================================================================
//  Properties
//=============================================================================
function ADC3Properties__create( THIS )
{
  return {};
}

function ADC3Properties__add__Real( THIS, local_value )
{
  THIS.push( local_value );
  return THIS;
}

function ADC3Properties__add__Logical( THIS, local_value )
{
  THIS.push( local_value );
  return THIS;
}

function ADC3Properties__add__Properties( THIS, local_value )
{
  THIS.push( local_value );
  return THIS;
}

function ADC3Properties__add__String( THIS, local_value )
{
  THIS.push( local_value );
  return THIS;
}

function ADC3Properties__clear( THIS )
{
  if (THIS.length)
  {
    THIS.length = 0;
  }
  else if (typeof(THIS) === "object")
  {
    for (var prop in THIS) { if (THIS.hasOwnProperty(prop)) { delete THIS[prop]; } }
  }
}

function ADC3Properties__count( THIS )
{
  if (THIS.length) return THIS.length;
  if (typeof(THIS) === "object") return Object.keys(THIS).length;
  return 0;
}

function ADC3Properties__get__Integer( THIS, local_index )
{
  return THIS[ local_index ];
}

function ADC3Properties__get__String( THIS, local_key )
{
  return THIS[ local_key ];
}

function ADC3Properties__get_Integer__Integer( THIS, local_index )
{
  var value = THIS[ local_index ];
  if (typeof(value) === "number")  return (value | 0);
  if (typeof(value) === "string")  return parseInt( value );
  if (typeof(value) === "boolean") return (value ? 1 : 0);
  return 0;
}

function ADC3Properties__get_Logical__Integer( THIS, local_index )
{
  return !!THIS[ local_index ];
}

function ADC3Properties__get_Real__Integer( THIS, local_index )
{
  var value = THIS[ local_index ];
  if (typeof(value) === "number")  return value;
  if (typeof(value) === "string")  return parseInt( value );
  if (typeof(value) === "boolean") return (value ? 1 : 0);
  return 0;
}

function ADC3Properties__get_String__Integer( THIS, local_index )
{
  var value = THIS[ local_index ];
  if (typeof(value) === "string")  return value;
  if (typeof(value) === "number")  return "" + value;
  if (typeof(value) === "boolean") return (value ? "true" : "false");
  return JSON.stringify(value);
}

function ADC3Properties__get_Integer__String( THIS, local_key )
{
  var value = THIS[ local_key ];
  if (typeof(value) === "number")  return (value | 0);
  if (typeof(value) === "string")  return parseInt( value );
  if (typeof(value) === "boolean") return (value ? 1 : 0);
  return 0;
}

function ADC3Properties__get_Logical__String( THIS, local_key )
{
  return !!THIS[ local_key ];
}

function ADC3Properties__get_Real__String( THIS, local_key )
{
  var value = THIS[ local_key ];
  if (typeof(value) === "number")  return value;
  if (typeof(value) === "string")  return parseInt( value );
  if (typeof(value) === "boolean") return (value ? 1 : 0);
  return 0;
}

function ADC3Properties__get_String__String( THIS, local_key )
{
  var value = THIS[ local_key ];
  if (typeof(value) === "string")  return value;
  if (typeof(value) === "number")  return "" + value;
  if (typeof(value) === "boolean") return (value ? "true" : "false");
  return JSON.stringify(value);
}

function ADC3Properties__set__Integer_Properties( THIS, local_index, local_new_value )
{
  THIS[ local_index ] = local_new_value;
}

function ADC3Properties__set__Integer_Integer( THIS, local_index, local_new_value )
{
  THIS[ local_index ] = local_new_value;
}

function ADC3Properties__set__Integer_Logical( THIS, local_index, local_new_value )
{
  THIS[ local_index ] = local_new_value;
}

function ADC3Properties__set__Integer_Real( THIS, local_index, local_new_value )
{
  THIS[ local_index ] = local_new_value;
}

function ADC3Properties__set__Integer_String( THIS, local_index, local_new_value )
{
  THIS[ local_index ] = local_new_value;
}

function ADC3Properties__set__String_Properties( THIS, local_key, local_new_value )
{
  THIS[ local_key ] = local_new_value;
}

function ADC3Properties__set__String_Integer( THIS, local_key, local_new_value )
{
  THIS[ local_key ] = local_new_value;
}

function ADC3Properties__set__String_Logical( THIS, local_key, local_new_value )
{
  THIS[ local_key ] = local_new_value;
}

function ADC3Properties__set__String_Real( THIS, local_key, local_new_value )
{
  THIS[ local_key ] = local_new_value;
}

function ADC3Properties__set__String_String( THIS, local_key, local_new_value )
{
  THIS[ local_key ] = local_new_value;
}

function ADC3Properties__to_String( THIS )
{
  return JSON.stringify( THIS );
}

function ADC3PropertyTable__create( THIS )
{
  return {};
}

function ADC3Properties__put__String_Properties( THIS, key, value )
{
  THIS[ key ] = value;
  return THIS;
}

/*
function ADC3Properties__put__String_List( THIS, key, list )
{
  var count = list.length;
  var plist = new Array( count );
  for (var i=0; i<count; ++i) plist[i] = list[i];
  THIS[key] = plist;
  return THIS;
}
*/

function ADC3PropertyList__create( THIS )
{
  return [];
}


//=============================================================================
//  Random
//=============================================================================
function ADC3Random__next_state__Real( THIS, state )
{
  // state = (state * 0xDEECe66d + 11) & 0xFFFFffffFFFF

  // KLUDGE - does not respect 'state'.  Works fine for random numbers but
  // this random sequence cannot be repeated or set to a specific seed value.
  var result = Math.random();
  if (result == 0) return 0.0001;
  return result;
}

//=============================================================================
//  Runtime
//=============================================================================
function ADC3Runtime__ip__Integer( THIS )
{
  return 0;
}

//=============================================================================
//  String
//=============================================================================
function ADC3String__create( THIS )
{
  return "";
}

function ADC3String__create__CharacterList( THIS, local_characters )
{
  var st = "";
  var count = local_characters.count;
  var data  = local_characters.data;
  for (var i=0; i<count; ++i) st += String.fromCharCode( data[i] );
  return st;
}

function ADC3String__compare_to__String( THIS, local_other )
{
  return THIS == local_other;
}

function ADC3String__count()
{
  alert( "String count" );
}

function ADC3String__get__Integer( local_index )
{
  alert( "String get" );
}

function ADC3String__hash_code( THIS )
{
  var count = THIS.length;
  var hash_code = 0;
  for (var i=0; i<count; ++i)
  {
    hash_code = hash_code*7 + THIS.charCodeAt(i);
  }
  return hash_code;
}


//=============================================================================
//  Time
//=============================================================================
function ADC3Time__current( THIS )
{
  return Date.now() / 1000.0;
}

