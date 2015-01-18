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

struct RogueMMAllocator;

struct RogueType
{
  RogueMMAllocator* allocator;

  //RogueType
};

struct RogueObject
{
  RogueObject* next_object;
  RogueObject* next_traced_object;
  RogueType*   type;
  int          size;
};

