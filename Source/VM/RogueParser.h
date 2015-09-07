//=============================================================================
//  RogueParser.h
//
//  2015.09.06 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_PARSER_H
#define ROGUE_PARSER_H

#include "Rogue.h"

struct RogueParser
{
  RogueAllocation allocation;
  RogueVM*        vm;
  RogueTokenizer* tokenizer;
};

RogueParser* RogueParser_create_with_filepath( RogueVM* vm, const char* filepath );
RogueParser* RogueParser_delete( RogueParser* THIS );

RogueLogical RogueParser_consume( RogueParser* THIS, RogueTokenType token_type );
RogueLogical RogueParser_consume_eols( RogueParser* THIS );
void         RogueParser_parse_elements( RogueParser* THIS );

#endif // ROGUE_PARSER_H

