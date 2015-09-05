//=============================================================================
//  RogueTokenizer.c
//
//  2015.09.04 by Abe Pralle
//=============================================================================
#include "Rogue.h"

//-----------------------------------------------------------------------------
//  Tokenizer Object
//-----------------------------------------------------------------------------
RogueTokenizer* RogueTokenizer_create_with_file( RogueVM* vm, RogueString* filepath )
{
  RogueTokenizer* tokenizer = RogueAllocator_allocate( &vm->allocator, sizeof(RogueTokenizer) );
  tokenizer->vm     = vm;
  tokenizer->reader = RogueParseReader_create( vm, filepath );
  return tokenizer;
}

RogueTokenizer* RogueTokenizer_delete( RogueTokenizer* THIS )
{
  RogueAllocator_free( &THIS->vm->allocator, THIS->tokens );
  RogueAllocator_free( &THIS->vm->allocator, THIS );
  return 0;
}

int RogueTokenizer_tokenize( RogueTokenizer* THIS )
{
  if ( !THIS->tokens ) THIS->tokens = RogueVMList_create( THIS->vm, 20 );
  THIS->tokens->count = 0;

  while (RogueTokenizer_tokenize_another(THIS))
  {
  }

  return 1;
}

int RogueTokenizer_tokenize_another( RogueTokenizer* THIS )
{
  RogueCharacter ch;
  RogueParseReader_consume_whitespace( THIS->reader );

  ch = RogueParseReader_peek( THIS->reader, 0 );
  if (ch == '\n')
  {
    RogueParseReader_read( THIS->reader );
    RogueVMList_add( THIS->tokens, RogueCmd_create(THIS->vm->cmd_type_EOL) );
  }

  return 0;
}
