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
  RogueAllocation   allocation;
  RogueVM*          vm;
  RogueParseReader* reader;
  RogueVMList*      tokens;
};

RogueTokenizer* RogueTokenizer_create_with_file( RogueVM* vm, RogueString* filepath );
RogueTokenizer* RogueTokenizer_delete( RogueTokenizer* THIS );
int             RogueTokenizer_tokenize( RogueTokenizer* THIS );
int             RogueTokenizer_tokenize_another( RogueTokenizer* THIS );

#endif // ROGUE_TOKENIZER_H
