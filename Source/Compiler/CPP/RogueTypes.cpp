//=============================================================================
//  RogueTypes.cpp
//
//  Definition of Rogue data types.
//
//  ---------------------------------------------------------------------------
//
//  Created 2015.01.15 by Abe Pralle
//
//  This is free and unencumbered software released into the public domain
//  under the terms of the UNLICENSE ( http://unlicense.org ).
//=============================================================================

#include <stdio.h>
#include <string.h>
#include "Rogue.h"
#include "RogueTypes.h"
#include "RogueIDTable.h"

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

  RogueString* st = (RogueString*) rogue.allocate_object( rogue.type_String, total_size );
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

