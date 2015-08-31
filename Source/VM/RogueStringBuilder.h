//=============================================================================
//  RogueStringBuilder.h
//
//  2015.08.31 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_STRING_BUILDER_H
#define ROGUE_STRING_BUILDER_H

#include "Rogue.h"

struct RogueStringBuilder
{
  RogueVM*        vm;
  RogueInteger    count;
  RogueInteger    capacity;
  RogueCharacter* characters;
  RogueCharacter  internal_buffer[1024];
};


#endif // ROGUE_STRING_BUILDER_H
