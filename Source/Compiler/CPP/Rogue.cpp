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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "Rogue.h"
#include "RogueProgram.h"

//-----------------------------------------------------------------------------
//  RogueType
//-----------------------------------------------------------------------------
RogueType::RogueType() : base_type_count(0), base_types(0), object_size(0), _singleton(0)
{
  if (rogue_program.next_type_index == rogue_program.type_count)
  {
    printf( "INTERNAL ERROR: Not enough type slots.\n" );
    exit( 1 );
  }

  rogue_program.types[ rogue_program.next_type_index++ ] = this;
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
  return rogue_program.allocate_object( this, object_size );
}

RogueLogical RogueType::instance_of( RogueType* ancestor_type )
{
  if (this == ancestor_type) return true;

  int count = base_type_count;
  RogueType** base_type_ptr = base_types - 1;
  while (--count >= 0)
  {
    if (this == *(++base_type_ptr)) return true;
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
  return (RogueObject*) rogue_program.allocate_object( this, sizeof(RogueObject) );
}

const char* RogueObjectType::name() { return "Object"; }

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

  RogueString* st = (RogueString*) rogue_program.allocate_object( rogue_program.type_RogueString, total_size );
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



RogueLogical RogueString::compare_to( RogueString* other )
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
  memcpy( result->characters+count, other->characters, count * sizeof(RogueCharacter) );

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

RogueString* RogueString::substring( RogueInteger i1, RogueInteger i2 )
{
  // Clamp i1 and i2
  if (i1 < 0) i1 = 0;
  if (i2 == -1 || i2 >= count) i2 = count - 1;

  // Return empty quotes if zero-length
  if (i1 > i2) return rogue_program.literal_strings[0]; // empty string

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

void RogueString::println( RogueString* st )
{
  if (st)
  {
    int count = st->count;
    RogueCharacter* src = st->characters - 1;
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
  printf( "\n" );
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

  RogueArray* array = (RogueArray*) rogue_program.allocate_object( rogue_program.type_RogueArray, total_size );

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
RogueProgramCore::RogueProgramCore( int type_count ) : objects(NULL), type_count(type_count), next_type_index(0)
{
  types = new RogueType*[ type_count ];
  memset( types, 0, sizeof(RogueType*) );

  type_RogueReal      = new RogueRealType();
  type_RogueFloat     = new RogueFloatType();
  type_RogueLong      = new RogueLongType();
  type_RogueInteger   = new RogueIntegerType();
  type_RogueCharacter = new RogueCharacterType();
  type_RogueByte      = new RogueByteType();
  type_RogueLogical   = new RogueLogicalType();

  type_RogueObject = new RogueObjectType();
  type_RogueString = new RogueStringType();
  type_RogueArray  = new RogueArrayType();
  pi = acos(-1);
}

RogueProgramCore::~RogueProgramCore()
{
  //printf( "~RogueProgramCore()\n" );

  while (objects)
  {
    RogueObject* next_object = objects->next_object;
    Rogue_allocator.free( objects, objects->size );
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
  obj->size = size;

  return obj;
}

void RogueProgramCore::collect_garbage()
{
  ROGUE_TRACE( main_object );

  RogueObject* cur = objects;
  objects = NULL;
  RogueObject* survivors = NULL;  // local var for speed

  while (cur)
  {
    RogueObject* next_object = cur->next_object;
    if (cur->reference_count > 0)
    {
      //printf( "Referenced %s\n", cur->type->name() );
      cur->next_object = survivors;
      survivors = cur;
    }
    else if (cur->size < 0)
    {
      //printf( "Referenced %s\n", cur->type->name() );
      cur->size = ~cur->size;
      cur->next_object = survivors;
      survivors = cur;
    }
    else
    {
      //printf( "Unreferenced %s\n", cur->type->name() );
      Rogue_allocator.free( cur, cur->size );
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
//  StringBuilder
//-----------------------------------------------------------------------------
RogueStringBuilder* RogueStringBuilder__print( RogueStringBuilder* buffer, const char* st )
{
  int len = strlen( st );
  RogueStringBuilder__reserve( buffer, len );

  RogueCharacter* dest = (buffer->characters->data->characters - 1) + buffer->characters->count;
  buffer->characters->count += len;

  const char* src = st - 1;
  while (--len >= 0)
  {
    *(++dest) = (RogueCharacter) *(++src);
  }

  return buffer;
}

RogueStringBuilder* RogueStringBuilder__print( RogueStringBuilder* buffer, RogueInteger value )
{
  char st[80];
  sprintf( st, "%d", value );
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

