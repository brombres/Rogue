//=============================================================================
//  RogueParser.c
//
//  2015.09.06 by Abe Pralle
//=============================================================================
#include "Rogue.h"

RogueParser* RogueParser_create_with_filepath( RogueVM* vm, const char* filepath )
{
  RogueParser* THIS = RogueAllocator_allocate( &vm->allocator, sizeof(RogueParser) );
  THIS->vm = vm;

  THIS->tokenizer = RogueTokenizer_create_with_file( vm, RogueVM_consolidate_c_string(vm,filepath) );

  return THIS;
}

RogueParser* RogueParser_delete( RogueParser* THIS )
{
  RogueTokenizer_delete( THIS->tokenizer );
  RogueAllocator_free( &THIS->vm->allocator, THIS );
  return 0;
}

RogueLogical RogueParser_consume( RogueParser* THIS, RogueTokenType token_type )
{
  if ( RogueTokenizer_peek_type(THIS->tokenizer,0) != token_type ) return 0;
  RogueTokenizer_read( THIS->tokenizer );
  return 1;
}

RogueLogical RogueParser_consume_eols( RogueParser* THIS )
{
  RogueLogical success = 0;
  while (RogueParser_consume(THIS,ROGUE_TOKEN_EOL))
  {
    success = 1;
  }
  return success;
}

void RogueParser_parse_elements( RogueParser* THIS )
{
  RogueTokenizer_tokenize( THIS->tokenizer );
  RogueParser_consume_eols( THIS );

  while (RogueTokenizer_has_another(THIS->tokenizer))
  {
    // Parse global 
    RogueCmd* cmd = RogueTokenizer_peek( THIS->tokenizer, 0 );

    RogueStringBuilder_print_c_string( &THIS->vm->error_message_builder, "Unexpected '" );
    cmd->type->print( cmd, &THIS->vm->error_message_builder );
    RogueStringBuilder_print_c_string( &THIS->vm->error_message_builder, "'." );

    RogueCmd_throw_error( cmd, 0 );
  }
}

