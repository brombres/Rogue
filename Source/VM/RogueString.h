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
RogueString* RogueString_create_from_c_string( RogueVM* vm, const char* utf8 );

void         RogueString_log( RogueString* THIS );
RogueString* RogueString_update_hash_code( RogueString* THIS );

RogueInteger RogueString_calculate_hash_code( const char* st );
RogueInteger RogueString_calculate_hash_code_for_characters( RogueCharacter* characters, RogueInteger count );

char* RogueString_to_c_string( RogueString* st );
// The C String is kept in an internal buffer and is valid until ether the next 
// RogueString_to_c_string() call or the VM is destroyed. 

#endif // ROGUE_STRING_H
