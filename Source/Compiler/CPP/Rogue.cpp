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
//RogueType::~RogueType()
//{
//}

//-----------------------------------------------------------------------------
//  RogueObject
//-----------------------------------------------------------------------------
//RogueObject* RogueObject::trace( RogueObject* obj, RogueObject* list )
//{
//
//}

//-----------------------------------------------------------------------------
//  RogueString
//-----------------------------------------------------------------------------
RogueString* RogueString::create( const char* c_string, int count )
{
  if (count == -1) count = strlen( c_string );

  int total_size = sizeof(RogueString) + ((count - 1) * sizeof(RogueCharacter));
  // RogueString already includes one character in its size.

  RogueString* st = (RogueString*) rogue_program.allocate_object( rogue_program.type_String, total_size );
  st->count = count;

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
//  RogueProgramCore
//-----------------------------------------------------------------------------
RogueProgramCore::RogueProgramCore() : objects(NULL)
{
  type_String = new RogueStringType();
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

  delete type_String;
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


