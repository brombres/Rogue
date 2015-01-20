#pragma once

//=============================================================================
//  Rogue.h
//
//  ---------------------------------------------------------------------------
//
//  Created 2015.01.19 by Abe Pralle
//
//  This is free and unencumbered software released into the public domain
//  under the terms of the UNLICENSE ( http://unlicense.org ).
//=============================================================================

#include "RogueTypes.h"

//-----------------------------------------------------------------------------
//  RogueCore
//-----------------------------------------------------------------------------
struct RogueCore
{
  RogueObject* objects;
  RogueObject* main_object;

  RogueCore();
  ~RogueCore();

  RogueObject* allocate_object( RogueType* type, int size );

  void         collect_garbage();
};

#include "RogueProgram.h"
