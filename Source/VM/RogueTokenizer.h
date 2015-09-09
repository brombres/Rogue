//=============================================================================
//  RogueTokenizer.h
//
//  2015.09.04 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_TOKENIZER_H
#define ROGUE_TOKENIZER_H

//-----------------------------------------------------------------------------
//  Tokenizer Object
//-----------------------------------------------------------------------------
struct RogueTokenizer
{
  RogueAllocation    allocation;
  RogueVM*           vm;
  RogueParseReader*  reader;
  RogueVMList*       tokens;
  RogueStringBuilder builder;
  RogueParsePosition parse_position;
  RogueInteger       position;
};

RogueTokenizer* RogueTokenizer_create_with_file( RogueVM* vm, RogueString* filepath );
RogueTokenizer* RogueTokenizer_delete( RogueTokenizer* THIS );

void            RogueTokenizer_add_token( RogueTokenizer* THIS, void* token );
RogueLogical    RogueTokenizer_next_is_real( RogueTokenizer* THIS );
RogueLogical    RogueTokenizer_tokenize( RogueTokenizer* THIS );
RogueLogical    RogueTokenizer_tokenize_another( RogueTokenizer* THIS );
void            RogueTokenizer_tokenize_integer_or_long( RogueTokenizer* THIS, RogueInteger base );
RogueCmdType*   RogueTokenizer_tokenize_symbol( RogueTokenizer* THIS );

RogueLogical    RogueTokenizer_has_another( RogueTokenizer* THIS );
RogueCmd*       RogueTokenizer_peek( RogueTokenizer* THIS, RogueInteger lookahead );
RogueCmdID  RogueTokenizer_peek_type( RogueTokenizer* THIS, RogueInteger lookahead );
RogueCmd*       RogueTokenizer_read( RogueTokenizer* THIS );

#endif // ROGUE_TOKENIZER_H
