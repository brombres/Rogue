//=============================================================================
//  Rogue.cpp
//
//  Rogue runtime.
//
//  ---------------------------------------------------------------------------
//
//  Created 2015.01.19 by Abe Pralle
//
//  This is free and unencumbered software released into the public domain
//  under the terms of the UNLICENSE ( http://unlicense.org ).
//=============================================================================

#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <inttypes.h>

#if !defined(_WIN32)
#  include <sys/time.h>
#  include <unistd.h>
#  include <signal.h>
#  include <dirent.h>
#  include <sys/socket.h>
#  include <sys/uio.h>
#  include <sys/stat.h>
#  include <netdb.h>
#  include <errno.h>
#  include <pthread.h>
#endif

#if defined(ANDROID)
#  include <netinet/in.h>
#endif

#if defined(_WIN32)
#  include <direct.h>
#  define chdir _chdir
#endif

#if TARGET_OS_IPHONE 
#  include <sys/types.h>
#  include <sys/sysctl.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "Rogue.h"
#include "RogueProgram.h"

#ifndef PATH_MAX
#  define PATH_MAX 4096
#endif

//-----------------------------------------------------------------------------
//  RogueSystemMessageQueue
//-----------------------------------------------------------------------------
RogueSystemMessageQueue::RogueSystemMessageQueue()
{
  write_list    = new RogueSystemList<RogueByte>( 1024 );
  read_list     = new RogueSystemList<RogueByte>( 1024 );
  read_position = 0;
  remaining_bytes_in_current = 0;
  message_size_location = -1;
}

RogueSystemMessageQueue::~RogueSystemMessageQueue()
{
  delete write_list;
  delete read_list;
}

RogueSystemMessageQueue* RogueSystemMessageQueue::begin_message( const char* message_type )
{
  update_message_size();  // Set the size of the previous message using the write position

  message_size_location = write_list->count;
  write_integer( 0 );  // placeholder for message size

  write_string( message_type );
  return this;
}

void RogueSystemMessageQueue::begin_reading()
{
  update_message_size();  // Set the size of the previous message using the write position

  RogueSystemList<RogueByte>* temp = read_list->clear();
  read_list = write_list;
  write_list = temp;
  read_position = 0;
  remaining_bytes_in_current = 0;
}

bool RogueSystemMessageQueue::has_another()
{
  if (remaining_bytes_in_current > 0)
  {
    read_position += remaining_bytes_in_current;
    remaining_bytes_in_current = 0;
  }

  if (read_position+4 >= read_list->count) return false;

  remaining_bytes_in_current = 4;  // enough for read_integer() to work so we can get the real value
  remaining_bytes_in_current = read_integer();
  if (remaining_bytes_in_current > (read_list->count - read_position))
  {
    remaining_bytes_in_current = (read_list->count - read_position);
  }
  return true;
}

RogueByte RogueSystemMessageQueue::read_byte()
{
  if ( !remaining_bytes_in_current ) return 0;
  --remaining_bytes_in_current;
  return (*read_list)[ read_position++ ];
}

RogueCharacter RogueSystemMessageQueue::read_character()
{
  RogueInteger result = read_byte();
  return   (result << 8) | read_byte();
}

RogueFloat RogueSystemMessageQueue::read_float()
{
  RogueInteger n = read_integer();
  return *((RogueFloat*)&n);
}

RogueInteger RogueSystemMessageQueue::read_int_x()
{
  RogueInteger b1 = read_byte();
  if (b1 <= 127)
  {
    // Use directly as 8-bit signed number (positive)
    // 0xxxxxxx
    return b1;
  }
  else if ((b1 & 0xc0) == 0xc0)
  {
    // Use directly as 8-bit signed number (negative)
    // 11xxxxxx
    return (char) b1;
  }
  else if ((b1 & 0xe0) == 0x80)
  {
    // 100x xxxx  xxxx xxxx - 13-bit signed value in 2 bytes
    RogueInteger result = ((b1 & 0x1F) << 8) | read_byte();
    if (result <= 4095) return result;  // 0 or positive
    return result - 8192;
  }
  else
  {
    // 101x xxxx  aaaaaaaa bbbbbbbb cccccccc dddddddd - 32-bit value in 5 bytes
    return read_integer();
  }
}

RogueInteger RogueSystemMessageQueue::read_integer()
{
  RogueInteger result = read_byte();
  result = (result << 8) | read_byte();
  result = (result << 8) | read_byte();
  return   (result << 8) | read_byte();
}

RogueLogical RogueSystemMessageQueue::read_logical()
{
  return (read_byte() != 0);
}

RogueLong RogueSystemMessageQueue::read_long()
{
  RogueLong result = read_integer();
  return (result << 32LL) | (((RogueLong)read_integer()) & 0xFFFFffffLL);
}

RogueReal RogueSystemMessageQueue::read_real()
{
  RogueLong n = read_long();
  return *((RogueReal*)&n);
}

int RogueSystemMessageQueue::read_string( char* buffer, int buffer_size )
{
  int count = read_int_x();
  if (count >= buffer_size)
  {
    for (int i=0; i<count; ++i) read_int_x();  // discard characters
    return -1;
  }
  else
  {
    char* dest = buffer-1;
    for (int i=0; i<count; ++i)
    {
      *(++dest) = (char) read_int_x();
    }
    buffer[count] = 0;
    return count;
  }
}

char* RogueSystemMessageQueue::read_new_c_string()
{
  int count = read_int_x();
  char* buffer = new char[ count + 1 ];

  char* dest = buffer-1;
  for (int i=0; i<count; ++i)
  {
    *(++dest) = (char) read_int_x();
  }
  buffer[count] = 0;

  return buffer;
}

RogueString* RogueSystemMessageQueue::read_string()
{
  int count = read_int_x();
  RogueString* st = RogueString::create( count );

  RogueCharacter* dest = st->characters - 1;
  for (int i=0; i<count; ++i)
  {
    *(++dest) = (RogueCharacter) read_int_x();
  }

  return st;
}

RogueSystemMessageQueue* RogueSystemMessageQueue::write_byte( int value )
{
  write_list->add( (RogueByte) value );
  return this;
}

RogueSystemMessageQueue* RogueSystemMessageQueue::write_character( int value )
{
  write_byte( value >> 8 );
  write_byte( value );
  return this;
}

RogueSystemMessageQueue* RogueSystemMessageQueue::write_float( float value)
{
  return write_integer( *((RogueInteger*)(&value)) );
}

RogueSystemMessageQueue* RogueSystemMessageQueue::write_int_x( int value )
{
  if (value >= -64 && value <= 127)
  {
    // 0xxx xxxx (0..127) OR
    // 11xx xxxx (-64..-1)
    write_list->add( (RogueByte) value );
    return this;
  }
  else if (value >= -4096 && value <= 4095)
  {
    // 100x xxxx  xxxx xxxx - 13-bit signed value in 2 bytes
    value &= 0x1Fff;
    write_list->add( (RogueByte) (0x80 | (value >> 8)) );
    write_list->add( (RogueByte) value );
    return this;
  }
  else
  {
    // 101x xxxx  aaaaaaaa bbbbbbbb cccccccc dddddddd - 32-bit value in 5 bytes
    write_list->add( (RogueByte) 0xA0 );
    return write_integer( value );
  }
}

RogueSystemMessageQueue* RogueSystemMessageQueue::write_integer( int value )
{
  write_list->add( (RogueByte) (value >> 24) );
  write_list->add( (RogueByte) (value >> 16) );
  write_list->add( (RogueByte) (value >> 8) );
  write_list->add( (RogueByte) value );
  return this;
}

RogueSystemMessageQueue* RogueSystemMessageQueue::write_logical( bool value )
{
  if (value) write_list->add( 1 );
  else       write_list->add( 0 );
  return this;
}

RogueSystemMessageQueue* RogueSystemMessageQueue::write_long( RogueLong value )
{
  write_integer( (int)(value >> 32LL) );
  return write_integer( (int)(value) );
}

RogueSystemMessageQueue* RogueSystemMessageQueue::write_real( double value )
{
  return write_long( *((RogueLong*)(&value)) );
}

RogueSystemMessageQueue* RogueSystemMessageQueue::write_string( const char* value )
{
  int len = strlen( value );
  write_int_x( len );
  for (int i=0; i<len; ++i) write_int_x( value[i] );
  return this;
}

RogueSystemMessageQueue* RogueSystemMessageQueue::write_string( RogueCharacter* value, int count )
{
  write_int_x( count );
  for (int i=0; i<count; ++i) write_int_x( value[i] );
  return this;
}

// INTERNAL USE
void RogueSystemMessageQueue::update_message_size()
{
  if (message_size_location >= 0)
  {
    int size = write_list->count - (message_size_location + 4);
    write_list->data[ message_size_location+0 ] = (RogueByte) (size >> 24);
    write_list->data[ message_size_location+1 ] = (RogueByte) (size >> 16);
    write_list->data[ message_size_location+2 ] = (RogueByte) (size >> 8);
    write_list->data[ message_size_location+3 ] = (RogueByte) size;
  }
}

//-----------------------------------------------------------------------------
//  RogueType
//-----------------------------------------------------------------------------
RogueType::RogueType() : base_type_count(0), base_types(0), index(-1), object_size(0), _singleton(0)
{
  if (Rogue_program.next_type_index == Rogue_program.type_count)
  {
    printf( "INTERNAL ERROR: Not enough type slots.\n" );
    exit( 1 );
  }

  Rogue_program.types[ Rogue_program.next_type_index++ ] = this;
}

RogueType::~RogueType()
{
  if (base_types)
  {
    delete base_types;
    base_types = 0;
    base_type_count = 0;
  }
}

RogueObject* RogueType::create_object()
{
  return Rogue_program.allocate_object( this, object_size );
}

RogueLogical RogueType::instance_of( RogueType* ancestor_type )
{
  if (this == ancestor_type) return true;

  int count = base_type_count;
  RogueType** base_type_ptr = base_types - 1;
  while (--count >= 0)
  {
    if (ancestor_type == *(++base_type_ptr)) return true;
  }

  return false;
}

RogueObject* RogueType::singleton()
{
  if ( !_singleton ) _singleton = create_object();
  return _singleton;
}

//-----------------------------------------------------------------------------
//  RogueObject
//-----------------------------------------------------------------------------
void RogueObjectType::configure()
{
  object_size = (int) sizeof( RogueObject );
}

RogueObject* RogueObjectType::create_object()
{
  return (RogueObject*) Rogue_program.allocate_object( this, sizeof(RogueObject) );
}

const char* RogueObjectType::name() { return "Object"; }


RogueObject* RogueObject::as( RogueObject* object, RogueType* specialized_type )
{
  if (object && object->type->instance_of(specialized_type)) return object;
  return NULL;
}

RogueLogical RogueObject::instance_of( RogueObject* object, RogueType* ancestor_type )
{
  return (!object || object->type->instance_of(ancestor_type));
}


//-----------------------------------------------------------------------------
//  RogueString
//-----------------------------------------------------------------------------
void RogueStringType::configure()
{
  object_size = (int) sizeof( RogueString );
}

RogueString* RogueString::create( int count )
{
  if (count < 0) count = 0;

  int total_size = sizeof(RogueString) + (count * sizeof(RogueCharacter));

  RogueString* st = (RogueString*) Rogue_program.allocate_object( Rogue_program.type_String, total_size );
  st->count = count;
  st->hash_code = 0;

  return st;
}

RogueString* RogueString::create( const char* c_string, int count )
{
  if (count == -1) count = strlen( c_string );

  RogueString* st = RogueString::create( count );

  // Copy 8-bit chars to 16-bit data while computing hash code.
  RogueCharacter* dest = st->characters - 1;
  const unsigned char* src = (const unsigned char*) (c_string - 1);
  int hash_code = 0;
  while (--count >= 0)
  {
    int ch = *(++src);
    *(++dest) = (RogueCharacter) ch;
    hash_code = ((hash_code << 3) - hash_code) + ch;  // hash * 7 + ch
  }

  st->hash_code = hash_code;

  return st;
}

RogueString* RogueString::create( RogueCharacterList* characters )
{
  if ( !characters ) return RogueString::create(0);

  int count = characters->count;
  RogueString* result = RogueString::create( characters->count );
  memcpy( result->characters, characters->data->characters, count*sizeof(RogueCharacter) );
  result->update_hash_code();
  return result;
}



RogueInteger RogueString::compare_to( RogueString* other )
{
  if (this == other) return 0;

  RogueInteger other_count = other->count;
  RogueInteger limit = count;

  int result;
  if (limit == other_count)
  {
    // Strings are same length
    result = memcmp( characters, other->characters, limit * sizeof(RogueCharacter) );
    if (result == 0) return 0;
  }
  else
  {
    // Strings differ in length.  Compare the part that matches first.
    if (limit > other_count) limit = other_count;
    result = memcmp( characters, other->characters, limit * sizeof(RogueCharacter) );
    if (result == 0)
    {
      // Equal so far - the shorter string comes before the longer one.
      if (limit == other_count) return 1;
      return -1;
    }
  }
  if (result < 0) return -1;
  else            return 1;
}


RogueLogical RogueString::contains( RogueString* substring, RogueInteger at_index )
{
  RogueInteger other_count = substring->count;
  if (at_index < 0 || at_index + other_count > this->count) return false;

  RogueCharacter* this_data  = characters;
  RogueCharacter* other_data = substring->characters;

  int i = -1;
  int i2 = other_count - 1;
  while (++i <= i2)
  {
    if (this_data[at_index+i] != other_data[i]) return false;
  }

  return true;
}

RogueInteger RogueString::locate( RogueCharacter ch, RogueInteger i1 )
{
  RogueInteger    limit = count;
  RogueCharacter* data  = characters;
  if (--i1 < -1) i1 = -1;

  while (++i1 < limit)
  {
    if (data[i1] == ch) return i1;
  }
  return -1;
}

RogueInteger RogueString::locate( RogueString* other, RogueInteger i1 )
{
  RogueInteger    other_count = other->count;
  if (other_count == 1) return locate( other->characters[0], i1 );

  RogueInteger    this_limit = (count - other_count) + 1;
  if (!other_count || this_limit <= 0) return -1;

  if (--i1 < -1) i1 = -1;
  while (++i1 < this_limit)
  {
    if (contains(other,i1)) return i1;
  }
  return -1;
}

RogueInteger RogueString::locate_last( RogueCharacter ch, RogueInteger i1 )
{
  RogueInteger    limit = count;
  RogueCharacter* data  = characters;

  int i = i1 + 1;
  if (i > limit) i = limit;

  while (--i >= 0)
  {
    if (data[i] == ch) return i;
  }
  return -1;
}

RogueInteger RogueString::locate_last( RogueString* other, RogueInteger i1 )
{
  RogueInteger    other_count = other->count;
  if (other_count == 1) return locate_last( other->characters[0], i1 );

  RogueInteger    this_limit = (count - other_count) + 1;
  if (!other_count || this_limit <= 0) return -1;

  int i = i1 + 1;
  if (i > this_limit) i = this_limit;

  while (--i >= 0)
  {
    if (contains(other,i)) return i;
  }
  return -1;
}

RogueString* RogueString::plus( const char* c_str )
{
  int len = strlen( c_str );

  RogueString* result = RogueString::create( count + len );
  memcpy( result->characters, characters, count * sizeof(RogueCharacter) );

  RogueCharacter* dest = result->characters + count;
  int code = hash_code;
  for (int i=0; i<len; ++i)
  {
    int ch = c_str[i];
    dest[i] = (RogueCharacter) ch;
    code = ((code << 3) - code) + ch;
  }

  result->hash_code = code;
  return result;
}

RogueString* RogueString::plus( char ch )
{
  return plus( (RogueCharacter) ch );
}

RogueString* RogueString::plus( RogueCharacter ch )
{
  RogueString* result = RogueString::create( count + 1 );
  memcpy( result->characters, characters, count * sizeof(RogueCharacter) );
  result->characters[count] = ch;
  result->hash_code = ((hash_code << 3) - hash_code) + ch;
  return result;
}

RogueString* RogueString::plus( RogueInteger value )
{
  char st[80];
  sprintf( st, "%d", value );
  return plus( st );
}

RogueString* RogueString::plus( RogueLong value )
{
  char st[80];
  sprintf( st, "%lld", value );
  return plus( st );
}

RogueString* RogueString::plus( RogueReal value )
{
  char st[80];
  sprintf( st, "%.4lf", value );
  return plus( st );
}

RogueString* RogueString::plus( RogueString* other )
{
  if ( !other ) other = RogueString::create( "null" );
  if (count == 0)        return other;
  if (other->count == 0) return this;

  RogueString* result = RogueString::create( count + other->count );
  memcpy( result->characters, characters, count * sizeof(RogueCharacter) );
  memcpy( result->characters+count, other->characters, other->count * sizeof(RogueCharacter) );

  int hash_count = other->count;

  int code = hash_code;
  while (hash_count >= 10)
  {
    code = code * 282475249;  // 7^10
    hash_count -= 10;
  }

  while (--hash_count >= 0) code = (code << 3) - code;

  result->hash_code = code + other->hash_code;
  return result;
}


bool RogueString::to_c_string( char* buffer, int buffer_size )
{
  if (count + 1 > buffer_size) return false;

  RogueCharacter* src = characters - 1;
  char* dest = buffer - 1;
  int n = count;

  while (--n >= 0)
  {
    *(++dest) = (char) (*(++src));
  }
  *(++dest) = 0;

  return true;
}

RogueInteger RogueString::to_integer()
{
  char buffer[80];
  if (to_c_string(buffer,80))
  {
    return strtol( buffer, NULL, 10 );
  }
  else
  {
    return 0;
  }
}

RogueReal    RogueString::to_real()
{
  char buffer[80];
  if (to_c_string(buffer,80))
  {
    return strtod( buffer, NULL );
  }
  else
  {
    return 0;
  }
}

RogueString* RogueString::substring( RogueInteger i1, RogueInteger i2 )
{
  // Clamp i1 and i2
  if (i1 < 0) i1 = 0;
  if (i2 == -1 || i2 >= count) i2 = count - 1;

  // Return empty quotes if zero-length
  if (i1 > i2) return Rogue_program.literal_strings[0]; // empty string

  int new_count = (i2 - i1) + 1;

  RogueString* result = RogueString::create( new_count );

  // Copy character substring while computing hash code.
  RogueCharacter* dest = result->characters - 1;
  RogueCharacter* src  = (characters + i1) - 1;
  int hash_code = 0;
  while (--new_count >= 0)
  {
    RogueCharacter ch = *(++src);
    *(++dest) = ch;
    hash_code = ((hash_code << 3) - hash_code) + ch;  // hash * 7 + ch
  }

  result->hash_code = hash_code;
  return result;
}

RogueString* RogueString::update_hash_code()
{
  int code = hash_code;
  int len = count;
  RogueCharacter* src = characters - 1;
  while (--len >= 0)
  {
    code = ((code<<3) - code) + *(++src);
  }
  hash_code = code;
  return this;
}

void RogueString::print( RogueString* st )
{
  if (st)
  {
    RogueString::print( st->characters, st->count );
  }
  else
  {
    printf( "null" );
  }
}

void RogueString::print( RogueCharacter* characters, int count )
{
  if (characters)
  {
    RogueCharacter* src = characters - 1;
    while (--count >= 0)
    {
      int ch = *(++src);
      putchar( ch );
    }
  }
  else
  {
    printf( "null" );
  }
}

//-----------------------------------------------------------------------------
//  RogueArray
//-----------------------------------------------------------------------------
void RogueArrayType::configure()
{
  object_size = (int) sizeof( RogueArray );
}

void RogueArrayType::trace( RogueObject* obj )
{
  RogueArray* array = (RogueArray*) obj;
  if ( !array->is_reference_array ) return;

  int count = array->count;
  RogueObject** cur = array->objects + count;
  while (--count >= 0)
  {
    ROGUE_TRACE( *(--cur) );
  }
}

RogueArray* RogueArray::create( int count, int element_size, bool is_reference_array )
{
  if (count < 0) count = 0;
  int data_size  = count * element_size;
  int total_size = sizeof(RogueArray) + data_size;

  RogueArray* array = (RogueArray*) Rogue_program.allocate_object( Rogue_program.type_Array, total_size );

  memset( array->bytes, 0, data_size );
  array->count = count;
  array->element_size = element_size;
  array->is_reference_array = is_reference_array;

  return array;
}

RogueArray* RogueArray::set( RogueInteger i1, RogueArray* other, RogueInteger other_i1, RogueInteger other_i2 )
{
  if ( !other || i1 >= count ) return this;
  if (this->is_reference_array ^ other->is_reference_array) return this;

  if (other_i2 == -1) other_i2 = other->count - 1;

  if (i1 < 0)
  {
    other_i1 -= i1;
    i1 = 0;
  }

  if (other_i1 < 0) other_i1 = 0;
  if (other_i2 >= other->count) other_i2 = other->count - 1;
  if (other_i1 > other_i2) return this;

  RogueByte* src = other->bytes + other_i1 * element_size;
  int other_bytes = ((other_i2 - other_i1) + 1) * element_size;

  RogueByte* dest = bytes + (i1 * element_size);
  int allowable_bytes = (count - i1) * element_size;

  if (other_bytes > allowable_bytes) other_bytes = allowable_bytes;

  if (src >= dest + other_bytes || (src + other_bytes) < dest)
  {
    // Copy region does not overlap
    memcpy( dest, src, other_bytes );
  }
  else
  {
    // Copy region overlaps
    memmove( dest, src, other_bytes );
  }

  return this;
}

//-----------------------------------------------------------------------------
//  RogueProgramCore
//-----------------------------------------------------------------------------
RogueProgramCore::RogueProgramCore( int type_count ) : objects(NULL), next_type_index(0)
{
  type_count += ROGUE_BUILT_IN_TYPE_COUNT;
  this->type_count = type_count;
  types = new RogueType*[ type_count ];
  memset( types, 0, sizeof(RogueType*) );

  type_Real      = new RogueRealType();
  type_Float     = new RogueFloatType();
  type_Long      = new RogueLongType();
  type_Integer   = new RogueIntegerType();
  type_Character = new RogueCharacterType();
  type_Byte      = new RogueByteType();
  type_Logical   = new RogueLogicalType();

  type_Object = new RogueObjectType();
  type_String = new RogueStringType();
  type_Array  = new RogueArrayType();

  type_FileReader = new RogueFileReaderType();
  type_FileWriter = new RogueFileWriterType();

  for (int i=0; i<next_type_index; ++i)
  {
    types[i]->configure();
  }

  pi = acos(-1);
}

RogueProgramCore::~RogueProgramCore()
{
  //printf( "~RogueProgramCore()\n" );

  while (objects)
  {
    RogueObject* next_object = objects->next_object;
    Rogue_allocator.free( objects, objects->object_size );
    objects = next_object;
  }

  for (int i=0; i<type_count; ++i)
  {
    if (types[i])
    {
      delete types[i];
      types[i] = 0;
    }
  }
}

RogueObject* RogueProgramCore::allocate_object( RogueType* type, int size )
{
  RogueObject* obj = (RogueObject*) Rogue_allocator.allocate( size );
  memset( obj, 0, size );

  obj->next_object = objects;
  objects = obj;
  obj->type = type;
  obj->object_size = size;

  return obj;
}

void RogueProgramCore::collect_garbage()
{
  ROGUE_TRACE( main_object );

  // Trace singletons
  for (int i=type_count-1; i>=0; --i)
  {
    RogueType* type = types[i];
    if (type) ROGUE_TRACE( type->_singleton );
  }

  // Trace through all as-yet unreferenced objects that are manually retained.
  RogueObject* cur = objects;
  while (cur)
  {
    if (cur->object_size >= 0 && cur->reference_count > 0)
    {
      ROGUE_TRACE( cur );
    }
    cur = cur->next_object;
  }

  cur = objects;
  objects = NULL;
  RogueObject* survivors = NULL;  // local var for speed

  while (cur)
  {
    RogueObject* next_object = cur->next_object;
    if (cur->object_size < 0)
    {
      // Discovered automatically during tracing.
      //printf( "Referenced %s\n", cur->type->name() );
      cur->object_size = ~cur->object_size;
      cur->next_object = survivors;
      survivors = cur;
    }
    else
    {
      //printf( "Unreferenced %s\n", cur->type->name() );
      Rogue_allocator.free( cur, cur->object_size );
    }
    cur = next_object;
  }

  objects = survivors;
}

RogueReal RogueProgramCore::mod( RogueReal a, RogueReal b )
{
  RogueReal q = a / b;
  return a - floor(q)*b;
}

RogueInteger RogueProgramCore::mod( RogueInteger a, RogueInteger b )
{
  if (!a && !b) return 0;

  if (b == 1) return 0;

  if ((a ^ b) < 0)
  {
    RogueInteger r = a % b;
    return r ? (r+b) : r;
  }
  else
  {
    return (a % b);
  }
}

RogueInteger RogueProgramCore::shift_right( RogueInteger value, RogueInteger bits )
{
  if (bits <= 0) return value;
  value >>= 1;
  if (--bits) return (value & 0x7fffFFFF) >> bits;
  else        return value;
}

//-----------------------------------------------------------------------------
//  RogueAllocationPage
//-----------------------------------------------------------------------------
RogueAllocationPage::RogueAllocationPage( RogueAllocationPage* next_page )
  : next_page(next_page)
{
  cursor = data;
  remaining = ROGUEMM_PAGE_SIZE;
  //printf( "New page\n");
}

void* RogueAllocationPage::allocate( int size )
{
  // Round size up to multiple of 8.
  if (size > 0) size = (size + 7) & ~7;
  else          size = 8;

  if (size > remaining) return NULL;

  //printf( "Allocating %d bytes from page.\n", size );
  void* result = cursor;
  cursor += size;
  remaining -= size;

  //printf( "%d / %d\n", ROGUEMM_PAGE_SIZE - remaining, ROGUEMM_PAGE_SIZE );
  return result;
}


//-----------------------------------------------------------------------------
//  RogueAllocator
//-----------------------------------------------------------------------------
RogueAllocator::RogueAllocator() : pages(NULL)
{
  for (int i=0; i<ROGUEMM_SLOT_COUNT; ++i)
  {
    free_objects[i] = NULL;
  }
}

RogueAllocator::~RogueAllocator()
{
  while (pages)
  {
    RogueAllocationPage* next_page = pages->next_page;
    delete pages;
    pages = next_page;
  }
}

void* RogueAllocator::allocate( int size )
{
  if (size > ROGUEMM_SMALL_ALLOCATION_SIZE_LIMIT) return malloc( size );

  if (size <= 0) size = ROGUEMM_GRANULARITY_SIZE;
  else           size = (size + ROGUEMM_GRANULARITY_MASK) & ~ROGUEMM_GRANULARITY_MASK;

  int slot = (size >> ROGUEMM_GRANULARITY_BITS);
  RogueObject* obj = free_objects[slot];
  
  if (obj)
  {
    //printf( "found free object\n");
    free_objects[slot] = obj->next_object;
    return obj;
  }

  // No free objects for requested size.

  // Try allocating a new object from the current page.
  if ( !pages )
  {
    pages = new RogueAllocationPage(NULL);
  }

  obj = (RogueObject*) pages->allocate( size );
  if (obj) return obj;


  // Not enough room on allocation page.  Allocate any smaller blocks
  // we're able to and then move on to a new page.
  int s = slot - 1;
  while (s >= 1)
  {
    obj = (RogueObject*) pages->allocate( s << ROGUEMM_GRANULARITY_BITS );
    if (obj)
    {
      //printf( "free obj size %d\n", (s << ROGUEMM_GRANULARITY_BITS) );
      obj->next_object = free_objects[s];
      free_objects[s] = obj;
    }
    else
    {
      --s;
    }
  }

  // New page; this will work for sure.
  pages = new RogueAllocationPage( pages );
  return pages->allocate( size );
}

void* RogueAllocator::allocate_permanent( int size )
{
  // Allocates arbitrary number of bytes (rounded up to a multiple of 8).
  // Intended for permanent use throughout the lifetime of the program.  
  // While such memory can and should be freed with free_permanent() to ensure
  // that large system allocations are indeed freed, small allocations 
  // will be recycled as blocks, losing 0..63 bytes in the process and
  // fragmentation in the long run.

  if (size > ROGUEMM_SMALL_ALLOCATION_SIZE_LIMIT) return malloc( size );

  // Round size up to multiple of 8.
  if (size <= 0) size = 8;
  else           size = (size + 7) & ~7;

  if ( !pages )
  {
    pages = new RogueAllocationPage(NULL);
  }

  void* result = pages->allocate( size );
  if (result) return result;

  // Not enough room on allocation page.  Allocate any smaller blocks
  // we're able to and then move on to a new page.
  int s = ROGUEMM_SLOT_COUNT - 2;
  while (s >= 1)
  {
    RogueObject* obj = (RogueObject*) pages->allocate( s << ROGUEMM_GRANULARITY_BITS );
    if (obj)
    {
      obj->next_object = free_objects[s];
      free_objects[s] = obj;
    }
    else
    {
      --s;
    }
  }

  // New page; this will work for sure.
  pages = new RogueAllocationPage( pages );
  return pages->allocate( size );
}

void* RogueAllocator::free( void* data, int size )
{
  if (data)
  {
    if (size > ROGUEMM_SMALL_ALLOCATION_SIZE_LIMIT)
    {
      ::free( data );
    }
    else
    {
      // Return object to small allocation pool
      RogueObject* obj = (RogueObject*) data;
      int slot = (size + ROGUEMM_GRANULARITY_MASK) >> ROGUEMM_GRANULARITY_BITS;
      if (slot <= 0) slot = 1;
      obj->next_object = free_objects[slot];
      free_objects[slot] = obj;
    }
  }

  // Always returns null, allowing a pointer to be freed and assigned null in
  // a single step.
  return NULL;
}

void* RogueAllocator::free_permanent( void* data, int size )
{
  if (data)
  {
    if (size > ROGUEMM_SMALL_ALLOCATION_SIZE_LIMIT)
    {
      ::free( data );
    }
    else
    {
      // Return object to small allocation pool if it's big enough.  Some
      // or all bytes may be lost until the end of the program when the
      // RogueAllocator frees its memory.
      RogueObject* obj = (RogueObject*) data;
      int slot = size >> ROGUEMM_GRANULARITY_BITS;
      if (slot)
      {
        obj->next_object = free_objects[slot];
        free_objects[slot] = obj;
      }
      // else memory is < 64 bytes and is unavailable.
    }
  }

  // Always returns null, allowing a pointer to be freed and assigned null in
  // a single step.
  return NULL;
}

RogueAllocator Rogue_allocator;


//-----------------------------------------------------------------------------
//  File
//-----------------------------------------------------------------------------
RogueString* RogueFile__absolute_filepath( RogueString* filepath_object )
{
  if ( !filepath_object ) return NULL;

  char filepath[ PATH_MAX ];
  filepath_object->to_c_string( filepath, PATH_MAX );

#if defined(_WIN32)
  {
    char long_name[PATH_MAX+4];
    char full_name[PATH_MAX+4];

    if (GetLongPathName(filepath, long_name, PATH_MAX+4) == 0)
    {
      strcpy_s( long_name, PATH_MAX+4, filepath );
    }

    if (GetFullPathName(long_name, PATH_MAX+4, full_name, 0) == 0)
    {
      // bail with name unchanged 
      return filepath_object;
    }

    return RogueString::create( full_name );
  }
#else

  bool is_folder = RogueFile__is_folder( filepath_object );

  {
    int original_dir_fd;
    int new_dir_fd;
    char filename[PATH_MAX];

    // A way to get back to the starting folder when finished.
    original_dir_fd = open( ".", O_RDONLY );  

    if (is_folder)
    {
      filename[0] = 0;
    }
    else
    {
      // fchdir only works with a path, not a path+filename (filepath).
      // Copy out the filename and null terminate the filepath to be just a path.
      int i = (int) strlen( filepath ) - 1;
      while (i >= 0 && filepath[i] != '/') --i;
      strcpy( filename, filepath+i+1 );
      filepath[i] = 0;
    }
    new_dir_fd = open( filepath, O_RDONLY );

    if (original_dir_fd >= 0 && new_dir_fd >= 0)
    {
      fchdir( new_dir_fd );
      getcwd( filepath, PATH_MAX );
      if ( !is_folder ) 
      {
        strcat( filepath, "/" );
        strcat( filepath, filename );
      }
      fchdir( original_dir_fd );
    }
    if (original_dir_fd >= 0) close( original_dir_fd );
    if (new_dir_fd >= 0) close( new_dir_fd );

    return RogueString::create( filepath );
  }
#endif
}

RogueLogical RogueFile__exists( RogueString* filepath )
{
  if ( !filepath ) return false;

  char path[ PATH_MAX ];
  filepath->to_c_string( path, PATH_MAX );

  FILE* fp = fopen( path, "rb" );
  if ( !fp ) return false;

  fclose( fp );
  return true;
}

RogueLogical RogueFile__is_folder( RogueString* filepath )
{
  if ( !filepath ) return false;

  char path[ PATH_MAX ];
  filepath->to_c_string( path, PATH_MAX );

#if defined(_WIN32)
  char filepath_copy[PATH_MAX];
  strcpy( filepath_copy, path );

  int path_len = strlen( path );
  int i = strlen(filepath_copy)-1;
  while (i > 0 && (filepath_copy[i] == '/' || filepath_copy[i] == '\\')) filepath_copy[i--] = 0;

  // Windows allows dir\* to count as a directory; guard against.
  for (i=0; filepath_copy[i]; ++i)
  {
    if (filepath_copy[i] == '*' || filepath_copy[i] == '?') return 0;
  }

  WIN32_FIND_DATA entry;
  HANDLE dir = FindFirstFile( filepath_copy, &entry );
  if (dir != INVALID_HANDLE_VALUE)
  {
    if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
      FindClose( dir );
      return 1;
    }
  }
  FindClose( dir );
  return 0;

#else
  DIR* dir = opendir( path );
  if ( !dir ) return 0;

  closedir( dir );
  return 1;
#endif
}


RogueString* RogueFile__load( RogueString* filepath )
{
  if ( !filepath ) return Rogue_program.literal_strings[0];

  char path[ PATH_MAX ];
  filepath->to_c_string( path, PATH_MAX );

  FILE* fp = fopen( path, "rb" );
  if ( !fp ) return Rogue_program.literal_strings[0];  // ""

  fseek( fp, 0, SEEK_END );
  int count = (int) ftell( fp );
  fseek( fp, 0, SEEK_SET );

  RogueString* result = RogueString::create( count );
  fread( result->characters, 1, count, fp );
  fclose( fp );

  unsigned char* src = ((unsigned char*)(result->characters)) + count;
  RogueCharacter* dest = result->characters + count;

  while (--count >= 0)
  {
    *(--dest) = *(--src);
  }

  result->update_hash_code();
  return result;
}

RogueLogical RogueFile__save( RogueString* filepath, RogueString* data )
{
  if ( !filepath || !data ) return false;

  char path[ PATH_MAX ];
  filepath->to_c_string( path, sizeof(path) );

  FILE* fp = fopen( path, "wb" );
  if ( !fp ) return false;

  // Reuse 'path' as a write buffer
  int remaining = data->count;
  RogueCharacter* src = data->characters - 1;

  while (remaining > 0)
  {
    unsigned char* dest = ((unsigned char*) path) - 1;
    int block_size = remaining;
    if (block_size > sizeof(path)) block_size = sizeof(path);
    int copy_count = block_size;
    while (--copy_count >= 0)
    {
      *(++dest) = (unsigned char) *(++src);
    }

    fwrite( path, 1, block_size, fp );
    remaining -= block_size;
  }

  fclose( fp );

  return true;
}

RogueInteger RogueFile__size( RogueString* filepath )
{
  if ( !filepath ) return 0;

  char path[ PATH_MAX ];
  filepath->to_c_string( path, sizeof(path) );

  FILE* fp = fopen( path, "rb" );
  if ( !fp ) return 0;

  fseek( fp, 0, SEEK_END );
  int size = (int) ftell( fp );
  fclose( fp );

  return size;
}


//-----------------------------------------------------------------------------
//  FileReader
//-----------------------------------------------------------------------------
void RogueFileReaderType::configure()
{
  object_size = (int) sizeof( RogueFileReader );
}

void RogueFileReaderType::trace( RogueObject* obj )
{
  ROGUE_TRACE( ((RogueFileReader*)obj)->filepath );
}

RogueFileReader* RogueFileReader__create( RogueString* filepath )
{
  RogueFileReader* reader = (RogueFileReader*) Rogue_program.type_FileReader->create_object();
  RogueFileReader__open( reader, filepath );
  return reader;
}

RogueFileReader* RogueFileReader__close( RogueFileReader* reader )
{
  if (reader && reader->fp)
  {
    fclose( reader->fp );
    reader->fp = NULL;
  }
  reader->position = reader->count = 0;
  return reader;
}

RogueInteger RogueFileReader__count( RogueFileReader* reader )
{
  if ( !reader ) return 0;
  return reader->count;
}

RogueLogical RogueFileReader__has_another( RogueFileReader* reader )
{
  return reader && (reader->position < reader->count);
}

RogueLogical RogueFileReader__open( RogueFileReader* reader, RogueString* filepath )
{
  RogueFileReader__close( reader );
  reader->filepath = filepath;

  if ( !filepath ) return false;

  char path[ PATH_MAX ];
  filepath->to_c_string( path, PATH_MAX );

  reader->fp = fopen( path, "rb" );
  if (reader->fp)
  {
    fseek( reader->fp, 0, SEEK_END );
    reader->count = (int) ftell( reader->fp );
    fseek( reader->fp, 0, SEEK_SET );

    if (reader->count == 0) RogueFileReader__close( reader );
  }

  return reader;
}

RogueCharacter RogueFileReader__peek( RogueFileReader* reader )
{
  if ( !reader || reader->position == reader->count ) return 0;

  if (reader->buffer_position == reader->buffer_count)
  {
    reader->buffer_count = (int) fread( reader->buffer, 1, sizeof(reader->buffer), reader->fp );
    reader->buffer_position = 0;
  }

  return reader->buffer[ reader->buffer_position ];
}

RogueInteger RogueFileReader__position( RogueFileReader* reader )
{
  if ( !reader ) return 0;
  return reader->position;
}

RogueCharacter RogueFileReader__read( RogueFileReader* reader )
{
  // Ugly duplication of peek() code for speed - otherwise there are a few too many checks
  if ( !reader || reader->position == reader->count ) return 0;

  if (reader->buffer_position == reader->buffer_count)
  {
    reader->buffer_count = (int) fread( reader->buffer, 1, sizeof(reader->buffer), reader->fp );
    reader->buffer_position = 0;
  }

  unsigned char result = reader->buffer[ reader->buffer_position++ ];
  if (++reader->position == reader->count) RogueFileReader__close( reader );
  return result;
}

RogueFileReader* RogueFileReader__set_position( RogueFileReader* reader, RogueInteger new_position )
{
  if ( !reader ) return NULL;

  if ( !reader->fp ) RogueFileReader__open( reader, reader->filepath );

  fseek( reader->fp, new_position, SEEK_SET );
  reader->position = new_position;

  reader->buffer_position = 0;
  reader->buffer_count = 0;

  return reader;
}


//-----------------------------------------------------------------------------
//  FileWriter
//-----------------------------------------------------------------------------
void RogueFileWriterType::configure()
{
  object_size = (int) sizeof( RogueFileWriter );
}

void RogueFileWriterType::trace( RogueObject* obj )
{
  ROGUE_TRACE( ((RogueFileWriter*)obj)->filepath );
}

RogueFileWriter* RogueFileWriter__create( RogueString* filepath )
{
  RogueFileWriter* writer = (RogueFileWriter*) Rogue_program.type_FileWriter->create_object();
  RogueFileWriter__open( writer, filepath );
  return writer;
}

RogueFileWriter* RogueFileWriter__close( RogueFileWriter* writer )
{
  if (writer && writer->fp)
  {
    RogueFileWriter__flush( writer );
    fclose( writer->fp );
    writer->fp = NULL;
  }
  writer->buffer_position = writer->count = 0;
  return writer;
}

RogueInteger RogueFileWriter__count( RogueFileWriter* writer )
{
  if ( !writer ) return 0;
  return writer->count;
}

RogueFileWriter* RogueFileWriter__flush( RogueFileWriter* writer )
{
  if ( !writer || !writer->buffer_position || !writer->fp ) return writer;

  fwrite( writer->buffer, 1, writer->buffer_position, writer->fp );
  writer->buffer_position = 0;
  return writer;
}

RogueLogical RogueFileWriter__open( RogueFileWriter* writer, RogueString* filepath )
{
  RogueFileWriter__close( writer );
  writer->filepath = filepath;

  if ( !filepath ) return false;

  char path[ PATH_MAX ];
  filepath->to_c_string( path, PATH_MAX );

  writer->fp = fopen( path, "wb" );
  return writer;
}

RogueFileWriter* RogueFileWriter__write( RogueFileWriter* writer, RogueCharacter ch )
{
  if ( !writer || !writer->fp ) return NULL;

  writer->buffer[ writer->buffer_position ] = (unsigned char) ch;
  if (++writer->buffer_position == sizeof(writer->buffer)) return RogueFileWriter__flush( writer );

  return writer;
}

//-----------------------------------------------------------------------------
//  Real
//-----------------------------------------------------------------------------
RogueReal RogueReal__create( RogueInteger high_bits, RogueInteger low_bits )
{
  RogueLong bits = high_bits;
  bits  = (bits << 32LL) | low_bits;
  return *((RogueReal*)&bits);
}

RogueInteger RogueReal__high_bits( RogueReal THIS )
{
  return (RogueInteger) (*((RogueLong*)&THIS) >> 32LL);
}

RogueInteger RogueReal__low_bits( RogueReal THIS )
{
  return (RogueInteger) *((RogueLong*)&THIS);
}


//-----------------------------------------------------------------------------
//  StringBuilder
//-----------------------------------------------------------------------------
RogueStringBuilder* RogueStringBuilder__print( RogueStringBuilder* buffer, const char* st )
{
  int len = strlen( st );
  RogueCharacter* dest = (buffer->characters->data->characters - 1) + buffer->characters->count;

  if (buffer->indent > 0)
  {
    int possible_indents = 1;
    int count = len;
    const char* src = st - 1;
    while (--count >= 0)
    {
      if (*(++src) == '\n') ++possible_indents;
    }

    int copy_count = 0;
    RogueStringBuilder__reserve( buffer, len + possible_indents*buffer->indent );
    src = st - 1;
    count = len;
    while (--count >= 0)
    {
      RogueCharacter ch = *(++src);
      if (ch == '\n')
      {
        buffer->at_newline = true;
      }
      else if (buffer->at_newline)
      {
        for (int i=buffer->indent; i>0; --i)
        {
          *(++dest) = ' ';
          ++copy_count;
        }
        buffer->at_newline = false;
      }
      *(++dest) = ch;
      ++copy_count;
    }
    buffer->characters->count += copy_count;
  }
  else
  {
    buffer->characters->count += len;

    const char* src = st - 1;

    while (--len >= 0)
    {
      *(++dest) = (RogueCharacter) *(++src);
    }

    if (len && *dest == '\n') buffer->at_newline = true;
  }

  return buffer;
}

RogueStringBuilder* RogueStringBuilder__print( RogueStringBuilder* buffer, RogueInteger value )
{
  char st[80];
  sprintf( st, "%d", value );
  return RogueStringBuilder__print( buffer, st );
}

RogueStringBuilder* RogueStringBuilder__print( RogueStringBuilder* buffer, RogueLong value )
{
  char st[80];
  sprintf( st, "%lld", value );
  return RogueStringBuilder__print( buffer, st );
}

RogueStringBuilder* RogueStringBuilder__print( RogueStringBuilder* buffer, RogueReal value, RogueInteger decimal_places )
{
  char format[80];
  char st[80];
  if (decimal_places > 40) decimal_places = 40;
  sprintf( format, "%%.%dlf", decimal_places );
  sprintf( st, format, value );
  return RogueStringBuilder__print( buffer, st );
}


//-----------------------------------------------------------------------------
//  System
//-----------------------------------------------------------------------------
void RogueSystem__exit( int result_code )
{
  exit( result_code );
}

//-----------------------------------------------------------------------------
//  Time
//-----------------------------------------------------------------------------
RogueReal RogueTime__current()
{
#if defined(_WIN32)
  struct __timeb64 time_struct;
  RogueReal time_seconds;
  _ftime64_s( &time_struct );
  time_seconds = (RogueReal) time_struct.time;
  time_seconds += time_struct.millitm / 1000.0;
  return time_seconds;

#else
  struct timeval time_struct;
  RogueReal time_seconds;
  gettimeofday( &time_struct, 0 );
  time_seconds = (RogueReal) time_struct.tv_sec;
  time_seconds += (time_struct.tv_usec / 1000000.0);
  return time_seconds;
#endif
}

