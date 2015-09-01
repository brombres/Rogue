//=============================================================================
//  RogueStringBuilder.h
//
//  2015.08.31 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_STRING_BUILDER_H
#define ROGUE_STRING_BUILDER_H

#include "Rogue.h"

#define ROGUE_STRING_BUILDER_INTERNAL_BUFFER_SIZE 1024

struct RogueStringBuilder
{
  RogueInteger    count;
  RogueInteger    capacity;
  RogueCharacter* characters;
  RogueCharacter  internal_buffer[ROGUE_STRING_BUILDER_INTERNAL_BUFFER_SIZE];
};

void RogueStringBuilder_init( RogueStringBuilder* THIS, RogueInteger initial_capacity );
void RogueStringBuilder_retire( RogueStringBuilder* THIS );


void RogueStringBuilder_log( RogueStringBuilder* THIS );

void RogueStringBuilder_print_character( RogueStringBuilder* THIS, RogueCharacter value );
void RogueStringBuilder_print_characters( RogueStringBuilder* THIS, RogueCharacter* values, RogueInteger len );
void RogueStringBuilder_print_integer( RogueStringBuilder* THIS, RogueInteger value );
void RogueStringBuilder_print_long( RogueStringBuilder* THIS, RogueLong value );
void RogueStringBuilder_print_object( RogueStringBuilder* THIS, RogueObject* value );
void RogueStringBuilder_print_real( RogueStringBuilder* THIS, RogueReal value );
void RogueStringBuilder_print_char( RogueStringBuilder* THIS, const char value );
void RogueStringBuilder_print_c_string( RogueStringBuilder* THIS, const char* value );
void RogueStringBuilder_print_string_builder( RogueStringBuilder* THIS, RogueStringBuilder* other );

void RogueStringBuilder_reserve( RogueStringBuilder* THIS, RogueInteger additional );

#endif // ROGUE_STRING_BUILDER_H
