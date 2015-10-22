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

#ifndef PATH_MAX
#  define PATH_MAX 4096
#endif


bool Rogue_program_configured = false;

//-----------------------------------------------------------------------------
//  RogueType
//-----------------------------------------------------------------------------
RogueType::RogueType() : base_type_count(0), base_types(0), index(-1), object_size(0), _singleton(0)
{
  /*
  if (Rogue_program.next_type_index == Rogue_program.type_count)
  {
    printf( "INTERNAL ERROR: Not enough type slots.\n" );
    exit( 1 );
  }
  */

  if (Rogue_program_configured) Rogue_program.types[ Rogue_program.next_type_index++ ] = this;
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
  if ( !_singleton )
  {
    _singleton = Rogue_program.allocate_object( this, object_size );
  }
  return _singleton;
}

//-----------------------------------------------------------------------------
//  RogueObject
//-----------------------------------------------------------------------------
void RogueObjectType::configure()
{
  object_size = (int) sizeof( RogueObject );
}

RogueObject* RogueObject::as( RogueObject* object, RogueType* specialized_type )
{
  if (object && object->type->instance_of(specialized_type)) return object;
  return NULL;
}

RogueLogical RogueObject::instance_of( RogueObject* object, RogueType* ancestor_type )
{
  return (!object || object->type->instance_of(ancestor_type));
}

void RogueObject_trace( void* obj )
{
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
}

void RogueString_trace( void* obj )
{
  if ( !obj || ((RogueObject*)obj)->object_size < 0 ) return;
  ((RogueObject*)obj)->object_size = ~((RogueObject*)obj)->object_size;
}

void RogueArray_trace( void* obj )
{
  RogueArray* array = (RogueArray*) obj;

  if ( !array || array->object_size < 0 ) return;
  array->object_size = ~array->object_size;

  if ( !array->is_reference_array ) return;

  int count = array->count;
  RogueObject** cur = array->objects + count;
  while (--count >= 0)
  {
    ROGUE_TRACE( *(--cur) );
  }
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
  type_count += ROGUE_BUILT_IN_TYPE_COUNT + 1000;
  this->type_count = type_count;
  types = new RogueType*[ type_count ];
  memset( types, 0, sizeof(RogueType*) );

  type_Object = new RogueObjectType();
  type_String = new RogueStringType();
  type_Array  = new RogueArrayType();

  for (int i=0; i<next_type_index; ++i)
  {
    types[i]->configure();
  }

Rogue_program_configured = true;
}

RogueProgramCore::~RogueProgramCore()
{
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

RogueAllocator Rogue_allocator;

void Rogue_configure_types()
{
  int i;
  int* type_info = Rogue_type_info_table - 1;

  for (i=0; i<Rogue_type_count; ++i)
  {
    int j;
    RogueType* type = &Rogue_types[i];

    type->_singleton = 0;

    type->index = i;
    type->object_size = Rogue_object_size_table[i];
    type->methods = Rogue_dynamic_method_table + *(++type_info);
    type->base_type_count = *(++type_info);
    if (type->base_type_count)
    {
      type->base_types = new RogueType*[ type->base_type_count ];
      for (j=0; j<type->base_type_count; ++j)
      {
        type->base_types[j] = &Rogue_types[ *(++type_info) ];
      } 
    }
    else
    {
      type->base_types = 0;
    }
    type->trace_fn = Rogue_trace_fn_table[i];
  }
}

