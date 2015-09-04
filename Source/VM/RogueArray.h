//=============================================================================
//  RogueArray.h
//
//  2015.09.02 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_ARRAY_H
#define ROGUE_ARRAY_H

RogueType* RogueTypeByteArray_create( RogueVM* vm );
RogueType* RogueTypeCharacterArray_create( RogueVM* vm );
RogueType* RogueTypeObjectArray_create( RogueVM* vm );

struct RogueArray
{
  RogueObject  object;
  RogueInteger count;
  union
  {
    RogueByte      bytes[0];
    RogueCharacter characters[0];
    void*          objects[0];
  };
};

RogueArray* RogueArray_create( RogueType* array_type, RogueInteger count );
RogueArray* RogueByteArray_create( RogueVM* vm, RogueInteger count );
RogueArray* RogueCharacterArray_create( RogueVM* vm, RogueInteger count );
RogueArray* RogueObjectArray_create( RogueVM* vm, RogueInteger count );

#endif // ROGUE_ARRAY_H
