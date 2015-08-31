//=============================================================================
//  RogueString.h
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_STRING_H
#define ROGUE_STRING_H

#include "Rogue.h"

struct RogueString
{
  RogueObject    object;
  RogueInteger   count;
  RogueInteger   hash_code;
  RogueCharacter characters[];
};

RogueString* RogueString_create( RogueVM* vm, int count );
RogueString* RogueString_from_utf8( RogueVM* vm, const char* utf8, int utf8_count );
RogueString* RogueString_update_hash_code( RogueString* THIS );

#endif // ROGUE_STRING_H
