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
#include "RogueTypes.h"
#include "RogueIDTable.h"

//-----------------------------------------------------------------------------
//  RogueType
//-----------------------------------------------------------------------------
RogueType::~RogueType()
{
}

RogueType* RogueType::init( int index, const char* name, int object_size )
{
  this->index = index;
  this->object_size = object_size;
  RogueIDTableEntry* entry = Rogue_id_table.get_entry( name );
  this->name_id = entry->id;
  this->name = entry->name;
  return this;
}

