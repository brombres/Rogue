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

RogueLogical RogueParser_consume( RogueParser* THIS, RogueCmdID id );
RogueLogical RogueParser_consume_eols( RogueParser* THIS );
void         RogueParser_must_consume( RogueParser* THIS, RogueCmdID id, const char* message );
void         RogueParser_must_consume_end_cmd( RogueParser* THIS );
void         RogueParser_parse_elements( RogueParser* THIS );
void*        RogueParser_parse_statement( RogueParser* THIS );
void*        RogueParser_parse_add_subtract( RogueParser* THIS, void* left );
void*        RogueParser_parse_expression( RogueParser* THIS );
void*        RogueParser_parse_term( RogueParser* THIS );

#endif // ROGUE_PARSER_H

