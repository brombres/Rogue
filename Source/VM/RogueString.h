//=============================================================================
//  RogueString.h
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_STRING_H
#define ROGUE_STRING_H

#include "Rogue.h"

RogueType* RogueTypeString_create( RogueVM* vm );

struct RogueString
{
  RogueObject    object;
  RogueInteger   count;
  RogueInteger   hash_code;
  RogueCharacter characters[];
};

RogueString* RogueString_create( RogueVM* vm, RogueInteger count );
RogueString* RogueString_create_from_characters( RogueVM* vm, RogueCharacter* characters, RogueInteger count );
RogueString* RogueString_create_from_utf8( RogueVM* vm, const char* utf8, int utf8_count );

void         RogueString_log( RogueString* THIS );
RogueString* RogueString_update_hash_code( RogueString* THIS );

#endif // ROGUE_STRING_H
