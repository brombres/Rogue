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
#include <string.h>
#include "Rogue.h"
#include "RogueAllocator.h"
#include "RogueProgram.h"

//-----------------------------------------------------------------------------
//  RogueCore
//-----------------------------------------------------------------------------
RogueCore::RogueCore() : objects(NULL)
{
  type_String = new RogueStringType();
}

RogueCore::~RogueCore()
{
  printf( "~RogueCore()\n" );

  while (objects)
  {
    RogueObject* next_object = objects->next_object;
    Rogue_allocator.free( objects, objects->size );
    objects = next_object;
  }

  delete type_String;
}

RogueObject* RogueCore::allocate_object( RogueType* type, int size )
{
  RogueObject* obj = (RogueObject*) Rogue_allocator.allocate( size );
  memset( obj, 0, size );

  obj->next_object = objects;
  objects = obj;
  obj->type = type;
  obj->size = size;

  return obj;
}

void RogueCore::collect_garbage()
{
  ROGUE_TRACE( main_object );
  int count = literal_string_count + 1;
  RogueString** cur_string_ptr = literal_strings - 1;
  while (--count)
  {
    RogueString* cur_string = *(++cur_string_ptr);
    ROGUE_TRACE( cur_string );
  }

  RogueObject* cur = objects;
  objects = NULL;
  RogueObject* survivors = NULL;  // local var for speed

  while (cur)
  {
    RogueObject* next_object = cur->next_object;
    if (cur->size >= 0)
    {
      printf( "Unreferenced %s\n", cur->type->name() );
      Rogue_allocator.free( cur, cur->size );
    }
    else
    {
      printf( "Referenced %s\n", cur->type->name() );
      cur->size = ~cur->size;
      cur->next_object = survivors;
      survivors = cur;
    }
    cur = next_object;
  }

  objects = survivors;
}

