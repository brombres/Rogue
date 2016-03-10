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
RogueLogical       Rogue_configured = 0;
RogueErrorHandler* Rogue_error_handler = 0;
RogueObject*       Rogue_error_object  = 0;
int                Rogue_bytes_allocated_since_gc = 0;
int                Rogue_argc;
const char**       Rogue_argv;
RogueCallStack     Rogue_call_stack;
RogueCallbackInfo  Rogue_on_begin_gc;
RogueCallbackInfo  Rogue_on_end_gc;


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

  if ( !size ) size = THIS->object_size;
  obj = RogueAllocator_allocate_object( THIS->allocator, THIS, size );

  if ((fn = THIS->init_object_fn)) return fn( obj );
  else                             return obj;
}

void RogueType_print_name( RogueType* THIS )
{
  char buffer[256];
  RogueString* st = Rogue_literal_strings[ THIS->name_index ];
  if (st)
  {
    RogueString_to_c_string( st, buffer, 256 );
    printf( "%s", buffer );
  }
}

RogueType* RogueType_retire( RogueType* THIS )
{
  if (THIS->base_types)
  {
    delete [] THIS->base_types;
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

  if ( !THIS ) return true;

  this_type = THIS->type;
  if (this_type == ancestor_type) return true;

  int count = this_type->base_type_count;
  RogueType** base_type_ptr = this_type->base_types - 1;
  while (--count >= 0)
  {
    if (ancestor_type == *(++base_type_ptr)) return true;
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

  int total_size = sizeof(RogueString) + (count * sizeof(RogueCharacter));

  RogueString* st = (RogueString*) RogueAllocator_allocate_object( RogueTypeString->allocator, RogueTypeString, total_size );
  st->count = count;
  st->hash_code = 0;

  return st;
}

RogueString* RogueString_create_from_c_string( const char* c_string, int count )
{
  if (count == -1) count = (int) strlen( c_string );

  RogueInt32 decoded_count = RogueString_decoded_utf8_count( c_string, count );

  RogueString* st = RogueString_create_with_count( decoded_count );
  RogueString_decode_utf8( c_string, count, st->characters );

  return RogueString_update_hash_code( st );
}

RogueString* RogueString_create_from_characters( RogueCharacterList* characters )
{
  if ( !characters ) return RogueString_create_with_count(0);

  int count = characters->count;
  RogueString* result = RogueString_create_with_count( characters->count );
  memcpy( result->characters, characters->data->characters, count*sizeof(RogueCharacter) );
  return RogueString_update_hash_code( result );
}

void RogueString_print_string( RogueString* st )
{
  if (st)
  {
    RogueString_print_characters( st->characters, st->count );
  }
  else
  {
    printf( "null" );
  }
}

void RogueString_decode_utf8( const char* utf8_data, RogueInt32 utf8_count, RogueCharacter* dest_buffer )
{
  RogueByte*      src  = (RogueByte*)(utf8_data - 1);
  RogueCharacter* dest = dest_buffer - 1;

  int remaining_count = utf8_count;
  while (--remaining_count >= 0)
  {
    int ch = *(++src);
    if (ch >= 0x80)
    {
      if ((ch & 0xe0) == 0xc0)
      {
        // 110x xxxx  10xx xxxx
        ch = ((ch & 0x1f) << 6) | (*(++src) & 0x3f);
        --remaining_count;
      }
      else if ((ch & 0xf0) == 0xe0)
      {
        // 1110 xxxx  10xx xxxx  10xx xxxx
        ch = ((ch & 0xf) << 6) | (*(++src) & 0x3f);
        ch = (ch << 6) | (*(++src) & 0x3f);
        remaining_count -= 2;
      }
      else
      {
        // 11110xxx 	10xxxxxx 	10xxxxxx 	10xxxxxx
        if (remaining_count >= 3)
        {
          ch = ((ch & 7) << 18) | ((src[1] & 0x3f) << 12);
          ch |= (src[2] & 0x3f) << 6;
          ch |= (src[3] & 0x3f);
          src += 3;
          remaining_count -= 3;
        }
        if (ch >= 0x10000)
        {
          // write surrogate pair
          ch -= 0x10000;
          dest[1] = (RogueCharacter) (0xd800 + ((ch >> 10) & 0x3ff));
          dest[2] = (RogueCharacter) (0xdc00 + (ch & 0x3ff));
          dest += 2;
          continue;
        }
        // else fall through and write a regular character
      }
    }
    *(++dest) = (RogueCharacter) ch;
  }
}

RogueInt32 RogueString_decoded_utf8_count( const char* utf8_data, RogueInt32 utf8_count )
{
  if (utf8_count == -1) utf8_count = (int) strlen( utf8_data );

  const char* cur   = utf8_data - 1;
  const char* limit = utf8_data + utf8_count;

  int result_count = 0;
  while (++cur < limit)
  {
    ++result_count;
    int ch = *((unsigned char*)cur);
    if (ch >= 0x80)
    {
      if ((ch & 0xe0) == 0xc0)      ++cur;
      else if ((ch & 0xf0) == 0xe0) cur += 2;
      else
      {
        if (cur+3 < limit)
        {
          // This 4-byte UTF-8 value will encode to either one or two characters.
          int low = ((unsigned char*)cur)[1];
          cur += 3;

          if ((ch & 7) || (low & 0x30))
          {
            // Decoded value is >= 0x10000 - will be written as a surrogate pair
            ++result_count;
          }
          //else Decoded value is <= 0xffff - will be written normally
        }
      }
    }
  }

  return result_count;
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
      else if (ch >= 0xd800 && ch <= 0xdbff && --count >= 0)
      {
        // 'ch' is the high surrogate beginning of a surrogate pair.
        // Together with the next 16-bit 'low' value, the pair forms
        // a single 21 bit value between 0x10000 and 0x10FFFF.
        int low = *(++src);
        if (low >= 0xdc00 and low <= 0xdfff)
        {
          int value = 0x10000 + (((ch - 0xd800)<<10) | (low-0xdc00));
          putchar( 0xf0 | ((value>>18) & 7) );
          putchar( 0x80 | ((value>>12) & 0x3f) );
          putchar( 0x80 | ((value>>6)  & 0x3f) );
          putchar( (value & 0x3f) | 0x80 );
        }
      }
      else
      {
        // %1110xxxx 10xxxxxx 10xxxxxx
        putchar( ((ch >> 12) & 15) | 0xe0 );
        putchar( ((ch >> 6) & 0x3f) | 0x80 );
        putchar( (ch & 0x3f) | 0x80 );
      }
    }
  }
  else
  {
    printf( "null" );
  }
}

bool RogueString_to_c_string( RogueString* THIS, char* buffer, int buffer_size )
{
  if (THIS->count + 1 > buffer_size) return false;

  RogueCharacter* src = THIS->characters - 1;
  char* dest = buffer - 1;
  int n = THIS->count;

  while (--n >= 0)
  {
    *(++dest) = (char) (*(++src));
  }
  *(++dest) = 0;

  return true;
}

RogueString* RogueString_update_hash_code( RogueString* THIS )
{
  int code = 0;
  int len = THIS->count;
  RogueCharacter* src = THIS->characters - 1;
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
    Rogue_bytes_allocated_since_gc += size;
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

  if (size <= 0) size = ROGUEMM_GRANULARITY_SIZE;
  else           size = (size + ROGUEMM_GRANULARITY_MASK) & ~ROGUEMM_GRANULARITY_MASK;

  Rogue_bytes_allocated_since_gc += size;

  int slot = (size >> ROGUEMM_GRANULARITY_BITS);
  ROGUE_DEF_LOCAL_REF(RogueObject*, obj, THIS->available_objects[slot]);

  if (obj)
  {
    //printf( "found free object\n");
    THIS->available_objects[slot] = obj->next_object;
    return obj;
  }

  // No free objects for requested size.

  // Try allocating a new object from the current page.
  if ( !THIS->pages )
  {
    THIS->pages = RogueAllocationPage_create(0);
  }

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

  // New page; this will work for sure.
  THIS->pages = RogueAllocationPage_create( THIS->pages );
  return RogueAllocationPage_allocate( THIS->pages, size );
}

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
  int i = Rogue_call_stack.count;
  if (i && leading_newline) printf("\n");
  while (--i >= 0)
  {
    printf( "%s\n", Rogue_call_stack.locations[i] );
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

    exit(0);
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
      type->base_types = new RogueType*[ type->base_type_count ];
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

bool Rogue_collect_garbage( bool forced )
{
  int i;

  if (!forced && Rogue_bytes_allocated_since_gc < ROGUE_GC_THRESHOLD_BYTES) return false;

  Rogue_on_begin_gc.call();

//printf( "GC %d\n", Rogue_bytes_allocated_since_gc );
  Rogue_bytes_allocated_since_gc = 0;

  Rogue_trace();

  for (i=0; i<Rogue_allocator_count; ++i)
  {
    RogueAllocator_collect_garbage( &Rogue_allocators[i] );
  }

  Rogue_on_end_gc.call();

  return true;
}

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

