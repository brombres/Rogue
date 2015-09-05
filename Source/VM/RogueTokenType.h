//=============================================================================
//  RogueTokenType.h
//
//  2015.09.04 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_TOKEN_H
#define ROGUE_TOKEN_H

#include "Rogue.h"

typedef enum RogueTokenType
{
  ROGUE_TOKEN_UNDEFINED,
  ROGUE_TOKEN_EOL,
  ROGUE_TOKEN_LITERAL_INTEGER
} RogueTokenType;

#endif // ROGUE_TOKEN_H
