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

#endif // ROGUE_STRING_H
