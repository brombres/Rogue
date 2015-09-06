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
  ROGUE_TOKEN_LITERAL_INTEGER,
  ROGUE_TOKEN_SYMBOL_CLOSE_PAREN,
  ROGUE_TOKEN_SYMBOL_EQ,
  ROGUE_TOKEN_SYMBOL_EQUALS,
  ROGUE_TOKEN_SYMBOL_EXCLAMATION,
  ROGUE_TOKEN_SYMBOL_GE,
  ROGUE_TOKEN_SYMBOL_GT,
  ROGUE_TOKEN_SYMBOL_LE,
  ROGUE_TOKEN_SYMBOL_LT,
  ROGUE_TOKEN_SYMBOL_NE,
  ROGUE_TOKEN_SYMBOL_OPEN_PAREN,
  ROGUE_TOKEN_SYMBOL_PLUS,
  ROGUE_TOKEN_SYMBOL_POUND,
} RogueTokenType;

#endif // ROGUE_TOKEN_H
