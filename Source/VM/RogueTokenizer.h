//=============================================================================
//  RogueTokenizer.h
//
//  2015.09.04 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_TOKENIZER_H
#define ROGUE_TOKENIZER_H

//-----------------------------------------------------------------------------
//  Tokenizer Type
//-----------------------------------------------------------------------------
RogueType* RogueTypeTokenizer_create( RogueVM* vm );


//-----------------------------------------------------------------------------
//  Tokenizer Object
//-----------------------------------------------------------------------------
struct RogueTokenizer
{
  RogueObject       object;
  RogueParseReader* reader;
};

RogueTokenizer* RogueTokenizer_create_with_file( RogueVM* vm, RogueString* filepath );

#endif // ROGUE_TOKENIZER_H
