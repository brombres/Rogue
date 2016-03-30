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
#include <exception>

#if !defined(ROGUE_PLATFORM_WINDOWS)
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

//-----------------------------------------------------------------------------
//  GLOBAL PROPERTIES
//-----------------------------------------------------------------------------
int                Rogue_gc_threshold = ROGUE_GC_THRESHOLD_DEFAULT;
RogueLogical       Rogue_configured = 0;
RogueObject*       Rogue_error_object  = 0;
int                Rogue_allocation_bytes_until_gc = Rogue_gc_threshold;
int                Rogue_argc;
const char**       Rogue_argv;
RogueDebugTrace*   Rogue_call_stack = 0;
RogueCallbackInfo  Rogue_on_begin_gc;
RogueCallbackInfo  Rogue_on_end_gc;
char               RogueDebugTrace::buffer[120];

//-----------------------------------------------------------------------------
//  RogueDebugTrace
//-----------------------------------------------------------------------------
RogueDebugTrace::RogueDebugTrace( const char* method_signature, const char* filename, int line )
  : method_signature(method_signature), filename(filename), line(line), previous_trace(0)
{
  previous_trace = Rogue_call_stack;
  Rogue_call_stack = this;
}

RogueDebugTrace::~RogueDebugTrace()
{
  Rogue_call_stack = previous_trace;
}

int RogueDebugTrace::count()
{
  int n = 1;
  RogueDebugTrace* current = previous_trace;
  while (current)
  {
    ++n;
    current = current->previous_trace;
  }
  return n;
}

char* RogueDebugTrace::to_c_string()
{
  sprintf( buffer, "[%s %s:%d]", method_signature, filename, line );
  return buffer;
}

//-----------------------------------------------------------------------------
//  RogueType
//-----------------------------------------------------------------------------
RogueArray* RogueType_create_array( int count, int element_size, bool is_reference_array )
{
  if (count < 0) count = 0;
  int data_size  = count * element_size;
  int total_size = sizeof(RogueArray) + data_size;

  RogueArray* array = (RogueArray*) RogueAllocator_allocate_object( RogueTypeArray->allocator, RogueTypeArray, total_size );

  memset( array->bytes, 0, data_size );
  array->count = count;
  array->element_size = element_size;
  array->is_reference_array = is_reference_array;

  return array;
}

RogueObject* RogueType_create_object( RogueType* THIS, RogueInt32 size )
{
  ROGUE_DEF_LOCAL_REF_NULL(RogueObject*, obj);
  RogueInitFn  fn;

  obj = RogueAllocator_allocate_object( THIS->allocator, THIS, size ? size : THIS->object_size );

  if ((fn = THIS->init_object_fn)) return fn( obj );
  else                             return obj;
}

bool RogueType_name_equals( RogueType* THIS, const char* name )
{
  // For debugging purposes
  RogueString* st = Rogue_literal_strings[ THIS->name_index ];
  if ( !st ) return false;

  return (0 == strcmp((char*)st->utf8,name));
}

void RogueType_print_name( RogueType* THIS )
{
  RogueString* st = Rogue_literal_strings[ THIS->name_index ];
  if (st)
  {
    printf( "%s", st->utf8 );
  }
}

RogueType* RogueType_retire( RogueType* THIS )
{
  if (THIS->base_types)
  {
#if !ROGUE_GC_MODE_BOEHM
    delete [] THIS->base_types;
#endif
    THIS->base_types = 0;
    THIS->base_type_count = 0;
  }

  return THIS;
}

RogueObject* RogueType_singleton( RogueType* THIS )
{
  RogueInitFn fn;

  if (THIS->_singleton) return THIS->_singleton;

  // NOTE: _singleton must be assigned before calling init_object()
  // so we can't just call RogueType_create_object().
  THIS->_singleton = RogueAllocator_allocate_object( THIS->allocator, THIS, THIS->object_size );

  if ((fn = THIS->init_object_fn)) THIS->_singleton = fn( THIS->_singleton );

  if ((fn = THIS->init_fn)) return fn( THIS->_singleton );
  else                      return THIS->_singleton;

  return THIS->_singleton;
}

//-----------------------------------------------------------------------------
//  RogueObject
//-----------------------------------------------------------------------------
RogueObject* RogueObject_as( RogueObject* THIS, RogueType* specialized_type )
{
  if (RogueObject_instance_of(THIS,specialized_type)) return THIS;
  return 0;
}

RogueLogical RogueObject_instance_of( RogueObject* THIS, RogueType* ancestor_type )
{
  RogueType* this_type;

  if ( !THIS )
  {
    return false;
  }

  this_type = THIS->type;
  if (this_type == ancestor_type)
  {
    return true;
  }

  int count = this_type->base_type_count;
  RogueType** base_type_ptr = this_type->base_types - 1;
  while (--count >= 0)
  {
    if (ancestor_type == *(++base_type_ptr))
    {
      return true;
    }
  }

  return false;
}

void* RogueObject_retain( RogueObject* THIS )
{
  ROGUE_INCREF(THIS);
  return THIS;
}

void* RogueObject_release( RogueObject* THIS )
{
  ROGUE_DECREF(THIS);
  return THIS;
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
  int count;
  RogueObject** src;
  RogueArray* array = (RogueArray*) obj;

  if ( !array || array->object_size < 0 ) return;
  array->object_size = ~array->object_size;

  if ( !array->is_reference_array ) return;

  count = array->count;
  src = array->objects + count;
  while (--count >= 0)
  {
    RogueObject* cur = *(--src);
    if (cur && cur->object_size >= 0)
    {
      cur->type->trace_fn( cur );
    }
  }
}

//-----------------------------------------------------------------------------
//  RogueString
//-----------------------------------------------------------------------------
RogueString* RogueString_create_with_count( int count )
{
  if (count < 0) count = 0;

  int total_size = sizeof(RogueString) + (count+1);

  RogueString* st = (RogueString*) RogueAllocator_allocate_object( RogueTypeString->allocator, RogueTypeString, total_size );
  st->count = count;
  st->character_count = -1;
  st->hash_code = 0;

  return st;
}

RogueString* RogueString_create_from_utf8( const char* utf8, int count )
{
  if (count == -1) count = (int) strlen( utf8 );

  RogueString* st = RogueString_create_with_count( count );
  memcpy( st->utf8, utf8, count );
  return RogueString_update_hash_code( st );
}

RogueString* RogueString_create_from_characters( RogueCharacter_List* characters )
{
  if ( !characters ) return RogueString_create_with_count(0);

  RogueCharacter* data = characters->data->characters;
  int count = characters->count;
  int utf8_count = 0;
  for (int i=count; --i>=0; )
  {
    RogueCharacter ch = data[i];
    if (ch <= 0x7F)         ++utf8_count;
    else if (ch <= 0x7FF)   utf8_count += 2;
    else if (ch <= 0xFFFF)  utf8_count += 3;
    else                    utf8_count += 4;
  }

  RogueString* result = RogueString_create_with_count( utf8_count );
  RogueByte*   dest = result->utf8;
  for (int i=0; i<count; ++i)
  {
    RogueCharacter ch = data[i];
    if (ch <= 0x7F)
    {
      *(dest++) = (RogueByte) ch;
    }
    else if (ch <= 0x7FF)
    {
      dest[0] = (RogueByte) (0xC0 | ((ch >> 6) & 0x1F));
      dest[1] = (RogueByte) (0x80 | (ch & 0x3F));
      dest += 2;
    }
    else if (ch <= 0xFFFF)
    {
      dest[0] = (RogueByte) (0xE0 | ((ch >> 12) & 0xF));
      dest[1] = (RogueByte) (0x80 | ((ch >> 6) & 0x3F));
      dest[2] = (RogueByte) (0x80 | (ch & 0x3F));
      dest += 3;
    }
    else
    {
      dest[0] = (RogueByte) (0xF0 | ((ch >> 18) & 0x7));
      dest[1] = (RogueByte) (0x80 | ((ch >> 12) & 0x3F));
      dest[2] = (RogueByte) (0x80 | ((ch >> 6) & 0x3F));
      dest[3] = (RogueByte) (0x80 | (ch & 0x3F));
      dest += 4;
    }
  }

  result->character_count = count;

  return RogueString_update_hash_code( result );
}

void RogueString_print_string( RogueString* st )
{
  if (st)
  {
    RogueString_print_utf8( st->utf8, st->count );
  }
  else
  {
    printf( "null" );
  }
}

void RogueString_print_characters( RogueCharacter* characters, int count )
{
  if (characters)
  {
    RogueCharacter* src = characters - 1;
    while (--count >= 0)
    {
      int ch = *(++src);

      if (ch < 0x80)
      {
        // %0xxxxxxx
        putchar( ch );
      }
      else if (ch < 0x800)
      {
        // %110xxxxx 10xxxxxx
        putchar( ((ch >> 6) & 0x1f) | 0xc0 );
        putchar( (ch & 0x3f) | 0x80 );
      }
      else if (ch <= 0xFFFF)
      {
        // %1110xxxx 10xxxxxx 10xxxxxx
        putchar( ((ch >> 12) & 15) | 0xe0 );
        putchar( ((ch >> 6) & 0x3f) | 0x80 );
        putchar( (ch & 0x3f) | 0x80 );
      }
      else
      {
        // Assume 21-bit
        // %11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        putchar( 0xf0 | ((ch>>18) & 7) );
        putchar( 0x80 | ((ch>>12) & 0x3f) );
        putchar( 0x80 | ((ch>>6)  & 0x3f) );
        putchar( (ch & 0x3f) | 0x80 );
      }
    }
  }
  else
  {
    printf( "null" );
  }
}

void RogueString_print_utf8( RogueByte* utf8, int count )
{
  --utf8;
  while (--count >= 0)
  {
    putchar( *(++utf8) );
  }
}


RogueString* RogueString_update_hash_code( RogueString* THIS )
{
  int code = 0;
  int len = THIS->count;
  RogueByte* src = THIS->utf8 - 1;
  while (--len >= 0)
  {
    code = ((code<<3) - code) + *(++src);
  }
  THIS->hash_code = code;
  return THIS;
}

//-----------------------------------------------------------------------------
//  RogueArray
//-----------------------------------------------------------------------------
RogueArray* RogueArray_set( RogueArray* THIS, RogueInt32 dest_i1, RogueArray* src_array, RogueInt32 src_i1, RogueInt32 copy_count )
{
  int element_size;
  int dest_i2, src_i2;

  if ( !src_array || dest_i1 >= THIS->count ) return THIS;
  if (THIS->is_reference_array ^ src_array->is_reference_array) return THIS;

  if (copy_count == -1) src_i2 = src_array->count - 1;
  else                  src_i2 = (src_i1 + copy_count) - 1;

  if (dest_i1 < 0)
  {
    src_i1 -= dest_i1;
    dest_i1 = 0;
  }

  if (src_i1 < 0) src_i1 = 0;
  if (src_i2 >= src_array->count) src_i2 = src_array->count - 1;
  if (src_i1 > src_i2) return THIS;

  copy_count = (src_i2 - src_i1) + 1;
  dest_i2 = dest_i1 + (copy_count - 1);
  if (dest_i2 >= THIS->count)
  {
    dest_i2 = (THIS->count - 1);
    copy_count = (dest_i2 - dest_i1) + 1;
  }
  if ( !copy_count ) return THIS;


#if defined(ROGUE_ARC)
  if (THIS != src_array || dest_i1 >= src_i1 + copy_count || (src_i1 + copy_count) <= dest_i1 || dest_i1 < src_i1)
  {
    // no overlap
    RogueObject** src  = src_array->objects + src_i1 - 1;
    RogueObject** dest = THIS->objects + dest_i1 - 1;
    while (--copy_count >= 0)
    {
      RogueObject* src_obj, dest_obj;
      if ((src_obj = *(++src))) ROGUE_INCREF(src_obj);
      if ((dest_obj = *(++dest)) && !(ROGUE_DECREF(dest_obj)))
      {
        // TODO: delete dest_obj
        *dest = src_obj;
      }
    }
  }
  else
  {
    // Copying earlier data to later data; copy in reverse order to
    // avoid accidental overwriting
    if (dest_i1 > src_i1)  // if they're equal then we don't need to copy anything!
    {
      RogueObject** src  = src_array->objects + src_i2 + 1;
      RogueObject** dest = THIS->objects + dest_i2 + 1;
      while (--copy_count >= 0)
      {
        RogueObject* src_obj, dest_obj;
        if ((src_obj = *(--src))) ROGUE_INCREF(src_obj);
        if ((dest_obj = *(--dest)) && !(ROGUE_DECREF(dest_obj)))
        {
          // TODO: delete dest_obj
          *dest = src_obj;
        }
      }
    }
  }
  return THIS;
#endif

  element_size = THIS->element_size;
  RogueByte* src = src_array->bytes + src_i1 * element_size;
  RogueByte* dest = THIS->bytes + (dest_i1 * element_size);
  int copy_bytes = copy_count * element_size;

  if (src == dest) return THIS;

  if (src >= dest + copy_bytes || (src + copy_bytes) <= dest)
  {
    // Copy region does not overlap
    memcpy( dest, src, copy_count * element_size );
  }
  else
  {
    // Copy region overlaps
    memmove( dest, src, copy_count * element_size );
  }

  return THIS;
}

//-----------------------------------------------------------------------------
//  RogueAllocationPage
//-----------------------------------------------------------------------------
RogueAllocationPage* RogueAllocationPage_create( RogueAllocationPage* next_page )
{
  RogueAllocationPage* result = (RogueAllocationPage*) ROGUE_NEW_BYTES(sizeof(RogueAllocationPage));
  result->next_page = next_page;
  result->cursor = result->data;
  result->remaining = ROGUEMM_PAGE_SIZE;
  return result;
}

RogueAllocationPage* RogueAllocationPage_delete( RogueAllocationPage* THIS )
{
  if (THIS) ROGUE_DEL_BYTES( THIS );
  return 0;
};

void* RogueAllocationPage_allocate( RogueAllocationPage* THIS, int size )
{
  // Round size up to multiple of 8.
  if (size > 0) size = (size + 7) & ~7;
  else          size = 8;

  if (size > THIS->remaining) return 0;

  //printf( "Allocating %d bytes from page.\n", size );
  void* result = THIS->cursor;
  THIS->cursor += size;
  THIS->remaining -= size;

  //printf( "%d / %d\n", ROGUEMM_PAGE_SIZE - remaining, ROGUEMM_PAGE_SIZE );
  return result;
}


//-----------------------------------------------------------------------------
//  RogueAllocator
//-----------------------------------------------------------------------------
RogueAllocator* RogueAllocator_create()
{
  RogueAllocator* result = (RogueAllocator*) ROGUE_NEW_BYTES( sizeof(RogueAllocator) );

  memset( result, 0, sizeof(RogueAllocator) );

  return result;
}

RogueAllocator* RogueAllocator_delete( RogueAllocator* THIS )
{
  while (THIS->pages)
  {
    RogueAllocationPage* next_page = THIS->pages->next_page;
    RogueAllocationPage_delete( THIS->pages );
    THIS->pages = next_page;
  }
  return 0;
}

void* RogueAllocator_allocate( RogueAllocator* THIS, int size )
{
#if ROGUE_GC_MODE_AUTO
  Rogue_collect_garbage();
#endif
  if (size > ROGUEMM_SMALL_ALLOCATION_SIZE_LIMIT)
  {
    Rogue_allocation_bytes_until_gc -= size;
    void * mem = ROGUE_NEW_BYTES(size);
#if ROGUE_GC_MODE_AUTO
    if (!mem)
    {
      // Try hard!
      Rogue_collect_garbage(true);
      mem = ROGUE_NEW_BYTES(size);
    }
#endif
    return mem;
  }

  size = (size > 0) ? (size + ROGUEMM_GRANULARITY_MASK) & ~ROGUEMM_GRANULARITY_MASK : ROGUEMM_GRANULARITY_SIZE;

  Rogue_allocation_bytes_until_gc -= size;

  int slot;
  ROGUE_DEF_LOCAL_REF(RogueObject*, obj, THIS->available_objects[(slot=(size>>ROGUEMM_GRANULARITY_BITS))]);

  if (obj)
  {
    //printf( "found free object\n");
    THIS->available_objects[slot] = obj->next_object;
    return obj;
  }

  // No free objects for requested size.

  // Try allocating a new object from the current page.
  if (THIS->pages )
  {
    obj = (RogueObject*) RogueAllocationPage_allocate( THIS->pages, size );
    if (obj) return obj;

    // Not enough room on allocation page.  Allocate any smaller blocks
    // we're able to and then move on to a new page.
    int s = slot - 1;
    while (s >= 1)
    {
      obj = (RogueObject*) RogueAllocationPage_allocate( THIS->pages, s << ROGUEMM_GRANULARITY_BITS );
      if (obj)
      {
        //printf( "free obj size %d\n", (s << ROGUEMM_GRANULARITY_BITS) );
        obj->next_object = THIS->available_objects[s];
        THIS->available_objects[s] = obj;
      }
      else
      {
        --s;
      }
    }
  }

  // New page; this will work for sure.
  THIS->pages = RogueAllocationPage_create( THIS->pages );
  return RogueAllocationPage_allocate( THIS->pages, size );
}

#if ROGUE_GC_MODE_BOEHM
void Rogue_Boehm_Finalizer( void* obj, void* data )
{
  RogueObject* o = (RogueObject*)obj;
  o->type->clean_up_fn(o);
}

RogueObject* RogueAllocator_allocate_object( RogueAllocator* THIS, RogueType* of_type, int size )
{
  // If we had more type information (e.g., whether the data contained
  // references), we could make better decisions here.
  // Also, it seems like we could probably use the small allocator too.
  RogueObject* obj = (RogueObject*)GC_MALLOC( size );
  if (!obj)
  {
    Rogue_collect_garbage( true );
    obj = (RogueObject*)GC_MALLOC( size );
  }
  ROGUE_GCDEBUG_STATEMENT( printf( "Allocating " ) );
  ROGUE_GCDEBUG_STATEMENT( RogueType_print_name(of_type) );
  ROGUE_GCDEBUG_STATEMENT( printf( " %p\n", (RogueObject*)obj ) );
  //ROGUE_GCDEBUG_STATEMENT( Rogue_print_stack_trace() );

  if (of_type->clean_up_fn)
  {
    GC_REGISTER_FINALIZER_IGNORE_SELF( obj, Rogue_Boehm_Finalizer, 0, 0, 0 );
  }

  memset( obj, 0, size );

  obj->type = of_type;
  obj->object_size = size;

  return obj;
}
#else
RogueObject* RogueAllocator_allocate_object( RogueAllocator* THIS, RogueType* of_type, int size )
{
  ROGUE_DEF_LOCAL_REF(RogueObject*, obj, (RogueObject*) RogueAllocator_allocate( THIS, size ));

  ROGUE_GCDEBUG_STATEMENT( printf( "Allocating " ) );
  ROGUE_GCDEBUG_STATEMENT( RogueType_print_name(of_type) );
  ROGUE_GCDEBUG_STATEMENT( printf( " %p\n", (RogueObject*)obj ) );
  //ROGUE_GCDEBUG_STATEMENT( Rogue_print_stack_trace() );

  memset( obj, 0, size );

  if (of_type->clean_up_fn)
  {
    obj->next_object = THIS->objects_requiring_cleanup;
    THIS->objects_requiring_cleanup = obj;
  }
  else
  {
    obj->next_object = THIS->objects;
    THIS->objects = obj;
  }
  obj->type = of_type;
  obj->object_size = size;

  return obj;
}
#endif

void* RogueAllocator_free( RogueAllocator* THIS, void* data, int size )
{
  if (data)
  {
    ROGUE_GCDEBUG_STATEMENT(memset(data,0,size));
    if (size > ROGUEMM_SMALL_ALLOCATION_SIZE_LIMIT)
    {
      ROGUE_DEL_BYTES( data );
    }
    else
    {
      // Return object to small allocation pool
      RogueObject* obj = (RogueObject*) data;
      int slot = (size + ROGUEMM_GRANULARITY_MASK) >> ROGUEMM_GRANULARITY_BITS;
      if (slot <= 0) slot = 1;
      obj->next_object = THIS->available_objects[slot];
      THIS->available_objects[slot] = obj;
    }
  }

  // Always returns null, allowing a pointer to be freed and assigned null in
  // a single step.
  return 0;
}


void RogueAllocator_free_objects( RogueAllocator* THIS )
{
  RogueObject* objects = THIS->objects;
  while (objects)
  {
    RogueObject* next_object = objects->next_object;
    RogueAllocator_free( THIS, objects, objects->object_size );
    objects = next_object;
  }

  THIS->objects = 0;
}

void RogueAllocator_collect_garbage( RogueAllocator* THIS )
{
  // Global program objects have already been traced through.

  // Trace through all as-yet unreferenced objects that are manually retained.
  RogueObject* cur = THIS->objects;
  while (cur)
  {
    if (cur->object_size >= 0 && cur->reference_count > 0)
    {
      cur->type->trace_fn( cur );
    }
    cur = cur->next_object;
  }

  cur = THIS->objects_requiring_cleanup;
  while (cur)
  {
    if (cur->object_size >= 0 && cur->reference_count > 0)
    {
      cur->type->trace_fn( cur );
    }
    cur = cur->next_object;
  }

  // For any unreferenced objects requiring clean-up, we'll:
  //   1.  Reference them and move them to a separate short-term list.
  //   2.  Finish the regular GC.
  //   3.  Call clean_up() on each of them, which may create new
  //       objects (which is why we have to wait until after the GC).
  //   4.  Move them to the list of regular objects.
  cur = THIS->objects_requiring_cleanup;
  RogueObject* unreferenced_clean_up_objects = 0;
  RogueObject* survivors = 0;  // local var for speed
  while (cur)
  {
    RogueObject* next_object = cur->next_object;
    if (cur->object_size < 0)
    {
      // Referenced.
      cur->object_size = ~cur->object_size;
      cur->next_object = survivors;
      survivors = cur;
    }
    else
    {
      // Unreferenced - go ahead and trace it since we'll call clean_up
      // on it.
      cur->type->trace_fn( cur );
      cur->next_object = unreferenced_clean_up_objects;
      unreferenced_clean_up_objects = cur;
    }
    cur = next_object;
  }
  THIS->objects_requiring_cleanup = survivors;


  // Reset or delete each general object
  cur = THIS->objects;
  THIS->objects = 0;
  survivors = 0;  // local var for speed

  while (cur)
  {
    RogueObject* next_object = cur->next_object;
    if (cur->object_size < 0)
    {
      cur->object_size = ~cur->object_size;
      cur->next_object = survivors;
      survivors = cur;
    }
    else
    {
      ROGUE_GCDEBUG_STATEMENT( printf( "Freeing " ) );
      ROGUE_GCDEBUG_STATEMENT( RogueType_print_name(cur->type) );
      ROGUE_GCDEBUG_STATEMENT( printf( " %p\n", cur ) );
      RogueAllocator_free( THIS, cur, cur->object_size );
    }
    cur = next_object;
  }

  THIS->objects = survivors;


  // Call clean_up() on unreferenced objects requiring cleanup
  // and move them to the general objects list so they'll be deleted
  // the next time they're unreferenced.  Calling clean_up() may
  // create additional objects so THIS->objects may change during a
  // clean_up() call.
  cur = unreferenced_clean_up_objects;
  while (cur)
  {
    RogueObject* next_object = cur->next_object;

    cur->type->clean_up_fn( cur );

    cur->object_size = ~cur->object_size;
    cur->next_object = THIS->objects;
    THIS->objects = cur;

    cur = next_object;
  }
}

void Rogue_print_stack_trace ( bool leading_newline )
{
  RogueDebugTrace* current = Rogue_call_stack;
  if (current && leading_newline) printf( "\n" );
  while (current)
  {
    printf( "%s\n", current->to_c_string() );
    current = current->previous_trace;
  }
  printf("\n");
}

void Rogue_segfault_handler( int signal, siginfo_t *si, void *arg )
{
    if (si->si_addr < (void*)4096)
    {
      // Probably a null pointer dereference.
      printf( "Null reference error (accessing memory at %p)\n",
              si->si_addr );
    }
    else
    {
      if (si->si_code == SEGV_MAPERR)
        printf( "Access to unmapped memory at " );
      else if (si->si_code == SEGV_ACCERR)
        printf( "Access to forbidden memory at " );
      else
        printf( "Unknown segfault accessing " );
      printf("%p\n", si->si_addr);
    }

    Rogue_print_stack_trace( true );

    exit(1);
}

void Rogue_configure_types()
{
  int i;
  int* type_info = Rogue_type_info_table - 1;

  // Install seg fault handler
  struct sigaction sa;

  memset( &sa, 0, sizeof(sa) );
  sigemptyset( &sa.sa_mask );
  sa.sa_sigaction = Rogue_segfault_handler;
  sa.sa_flags     = SA_SIGINFO;

  sigaction( SIGSEGV, &sa, NULL );

  // Initialize allocators
  memset( Rogue_allocators, 0, sizeof(RogueAllocator)*Rogue_allocator_count );

  // Initialize types
  for (i=0; i<Rogue_type_count; ++i)
  {
    int j;
    RogueType* type = &Rogue_types[i];

    memset( type, 0, sizeof(RogueType) );

    type->index = i;
    type->name_index = Rogue_type_name_index_table[i];
    type->object_size = Rogue_object_size_table[i];
    type->allocator = &Rogue_allocators[ *(++type_info) ];
    type->methods = Rogue_dynamic_method_table + *(++type_info);
    type->base_type_count = *(++type_info);
    if (type->base_type_count)
    {
#if ROGUE_GC_MODE_BOEHM
      type->base_types = new (NoGC) RogueType*[ type->base_type_count ];
#else
      type->base_types = new RogueType*[ type->base_type_count ];
#endif
      for (j=0; j<type->base_type_count; ++j)
      {
        type->base_types[j] = &Rogue_types[ *(++type_info) ];
      }
    }
    type->trace_fn = Rogue_trace_fn_table[i];
    type->init_object_fn = Rogue_init_object_fn_table[i];
    type->init_fn        = Rogue_init_fn_table[i];
    type->clean_up_fn    = Rogue_clean_up_fn_table[i];
  }
}

#if ROGUE_GC_MODE_BOEHM
static GC_ToggleRefStatus Rogue_Boehm_ToggleRefStatus( void * o )
{
  RogueObject* obj = (RogueObject*)o;
  if (obj->reference_count > 0) return GC_TOGGLE_REF_STRONG;
  return GC_TOGGLE_REF_DROP;
}

static void Rogue_Boehm_on_collection_event( GC_EventType event )
{
  if (event == GC_EVENT_START)
  {
    Rogue_on_begin_gc.call();
  }
  else if (event == GC_EVENT_END)
  {
    Rogue_on_end_gc.call();
  }
}

void Rogue_configure_gc()
{
  // Initialize Boehm collector
  //GC_set_finalize_on_demand(0);
  GC_set_toggleref_func(Rogue_Boehm_ToggleRefStatus);
  GC_set_on_collection_event(Rogue_Boehm_on_collection_event);
  //GC_set_all_interior_pointers(0);
  GC_INIT();
}
#else
void Rogue_configure_gc()
{
}
#endif

#if ROGUE_GC_MODE_BOEHM
bool Rogue_collect_garbage( bool forced )
{
  if (forced)
  {
    GC_gcollect();
    return true;
  }

  return GC_collect_a_little();
}
#else
bool Rogue_collect_garbage( bool forced )
{
  int i;

  if (Rogue_allocation_bytes_until_gc > 0 && !forced) return false;

  Rogue_on_begin_gc.call();

//printf( "GC %d\n", Rogue_allocation_bytes_until_gc );
  Rogue_allocation_bytes_until_gc = Rogue_gc_threshold;

  Rogue_trace();

  for (i=0; i<Rogue_allocator_count; ++i)
  {
    RogueAllocator_collect_garbage( &Rogue_allocators[i] );
  }

  Rogue_on_end_gc.call();

  return true;
}
#endif

void Rogue_quit()
{
  int i;

  if ( !Rogue_configured ) return;
  Rogue_configured = 0;

  for (i=0; i<Rogue_allocator_count; ++i)
  {
    RogueAllocator_free_objects( &Rogue_allocators[i] );
  }

  for (i=0; i<Rogue_type_count; ++i)
  {
    RogueType_retire( &Rogue_types[i] );
  }
}

#if ROGUE_GC_MODE_BOEHM

void Rogue_Boehm_IncRef (RogueObject* o)
{
  ++o->reference_count;
  if (o->reference_count == 1)
  {
    GC_toggleref_add(o, 1);
  }
}
void Rogue_Boehm_DecRef (RogueObject* o)
{
  --o->reference_count;
  if (o->reference_count < 0)
  {
    o->reference_count = 0;
  }
}
#endif


//-----------------------------------------------------------------------------
//  Exception handling
//-----------------------------------------------------------------------------
void Rogue_terminate_handler ()
{
  if (Rogue_error_object && Rogue_error_object->type)
  {
    printf( "Uncaught " );
    RogueType_print_name( Rogue_error_object->type );
    printf( ".\n\n" );

    RogueStackTrace__print( ((RogueClassException*)Rogue_error_object)->stack_trace );
  }
  else
  {
    printf( "Uncaught error.\n" );
  }
  exit(1);
}
