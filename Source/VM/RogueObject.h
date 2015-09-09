//=============================================================================
//  RogueObject.h
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_OBJECT_H
#define ROGUE_OBJECT_H

struct RogueObject
{
  RogueAllocation allocation;
  RogueType*      type;
};

void* RogueObject_create( RogueType* type, RogueInteger size );

RogueLogical RogueObject_equals_c_string( void* THIS, const char* c_string );
RogueLogical RogueObject_equals_characters( void* THIS, RogueCharacter* characters,
                                            RogueInteger count, RogueInteger hash_code );
RogueInteger RogueObject_hash_code( void* THIS );
void         RogueObject_print( void* THIS );
void         RogueObject_println( void* THIS );

#endif // ROGUE_OBJECT_H
