//=============================================================================
//  RogueTokenizer.c
//
//  2015.09.04 by Abe Pralle
//=============================================================================
#include "Rogue.h"

//-----------------------------------------------------------------------------
//  Tokenizer Type
//-----------------------------------------------------------------------------
RogueType* RogueTypeTokenizer_create( RogueVM* vm )
{
  RogueType* THIS = RogueType_create( vm, "Tokenizer", sizeof(RogueTokenizer) );
  return THIS;
}


//-----------------------------------------------------------------------------
//  Tokenizer Object
//-----------------------------------------------------------------------------
RogueTokenizer* RogueTokenizer_create_with_file( RogueVM* vm, RogueString* filepath )
{
  RogueTokenizer* tokenizer = RogueObject_create( vm->type_Tokenizer, sizeof(RogueTokenizer) );
  tokenizer->reader = RogueParseReader_create( vm, filepath );
  return tokenizer;
}

