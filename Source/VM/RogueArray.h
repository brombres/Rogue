//=============================================================================
//  RogueArray.h
//
//  2015.09.02 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_ARRAY_H
#define ROGUE_ARRAY_H

RogueType* RogueTypeObjectArray_create( RogueVM* vm );

struct RogueArray
{
  RogueObject  object;
  RogueInteger count;
  union
  {
    RogueObject* objects[0];
  };
};

RogueArray* RogueArray_create( RogueType* array_type, RogueInteger count );

#endif // ROGUE_ARRAY_H
