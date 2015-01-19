#pragma once
//=============================================================================
//  RogueTypes.h
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

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <cstdint>
#endif

#if defined(_WIN32)
  typedef double           RogueReal;
  typedef float            RogueFloat;
  typedef __int64          RogueLong;
  typedef __int32          RogueInteger;
  typedef unsigned __int16 RogueCharacter;
  typedef unsigned char    RogueByte;
  typedef RogueInteger     RogueLogical;
#else
  typedef double           RogueReal;
  typedef float            RogueFloat;
  typedef int64_t          RogueLong;
  typedef int32_t          RogueInteger;
  typedef uint16_t         RogueCharacter;
  typedef uint8_t          RogueByte;
  typedef RogueInteger     RogueLogical;
#endif

struct RogueAllocator;


//-----------------------------------------------------------------------------
//  RogueType
//-----------------------------------------------------------------------------
struct RogueType
{
  int index;
  int object_size;
  int name_id;
  const char* name;

  ~RogueType();

  RogueType* init( int index, const char* name, int object_size );
};


//-----------------------------------------------------------------------------
//  RogueObject
//-----------------------------------------------------------------------------
struct RogueObject
{
  RogueObject* next_object;
  // Used to keep track of this allocation so that it can be freed when no
  // longer referenced.

  RogueObject* next_traced_object;
  // Used during GC to build a list of objects to trace.

  RogueType*   type;
  // Type info for this object.

  RogueInteger size;
  // Set to be ~size when traced through during a garbage collection,
  // then flipped back again at the end of GC.

  RogueInteger reference_count;
  // A positive reference_count ensures that this object will never be
  // collected.  A zero reference_count means this object is kept as long as
  // it is visible to the memory manager.
};

