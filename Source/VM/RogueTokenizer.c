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

RogueLogical RogueTokenizer_tokenize( RogueTokenizer* THIS )
{
  if ( !THIS->tokens ) THIS->tokens = RogueVMList_create( THIS->vm, 20 );
  THIS->tokens->count = 0;

  while (RogueTokenizer_tokenize_another(THIS))
  {
  }

  return 1;
}

RogueLogical RogueTokenizer_tokenize_another( RogueTokenizer* THIS )
{
  RogueCharacter ch;
  RogueParseReader_consume_whitespace( THIS->reader );
  if ( !RogueParseReader_has_another(THIS->reader) ) return 0;

  ch = RogueParseReader_peek( THIS->reader, 0 );
  if (ch == '\n')
  {
    RogueParseReader_read( THIS->reader );
    RogueVMList_add( THIS->tokens, RogueCmd_create(THIS->vm->cmd_type_eol) );
    return 1;
  }
  
  {
    RogueCmdType* type = RogueTokenizer_tokenize_symbol( THIS );
    if (type)
    {
      RogueVMList_add( THIS->tokens, RogueCmd_create(type) );
    }
    // else next token was parsed (otherwise exception would be thrown), but the
    // method handled adding it to the token list since it required special handling.
    return 1;
  }

  return 0;
}

RogueCmdType* RogueTokenizer_tokenize_symbol( RogueTokenizer* THIS )
{
  RogueParsePosition pos = THIS->reader->position;
  RogueCharacter ch = RogueParseReader_read( THIS->reader );
  RogueVM* vm = THIS->vm;
  switch (ch)
  {
    case '!':
      if (RogueParseReader_consume(THIS->reader,'=')) return vm->cmd_type_symbol_ne;
      return vm->cmd_type_symbol_exclamation;
    case '#':
      return vm->cmd_type_symbol_pound;
    case '<':
      if (RogueParseReader_consume(THIS->reader,'=')) return vm->cmd_type_symbol_le;
      return vm->cmd_type_symbol_lt;
    case '=':
      if (RogueParseReader_consume(THIS->reader,'=')) return vm->cmd_type_symbol_eq;
      return vm->cmd_type_symbol_equals;
    case '>':
      if (RogueParseReader_consume(THIS->reader,'=')) return vm->cmd_type_symbol_ge;
      return vm->cmd_type_symbol_gt;
  }

  RogueStringBuilder_print_c_string( &vm->error_message_builder, "Unexpected character '" );
  RogueStringBuilder_print_character( &vm->error_message_builder, ch ); 
  RogueStringBuilder_print_c_string( &vm->error_message_builder, "'." );
  ROGUE_THROW( vm, THIS->reader->filepath, pos, 0 );

  return 0;
}

RogueLogical RogueTokenizer_has_another( RogueTokenizer* THIS )
{
  return (THIS->position < THIS->tokens->count);
}

RogueCmd* RogueTokenizer_peek( RogueTokenizer* THIS, RogueInteger lookahead )
{
  RogueInteger index = THIS->position + lookahead;
  if (index < THIS->tokens->count) return THIS->tokens->array->objects[index];
  else                             return 0;
}

RogueTokenType RogueTokenizer_peek_type( RogueTokenizer* THIS, RogueInteger lookahead )
{
  RogueInteger index = THIS->position + lookahead;
  if (index < THIS->tokens->count) return ((RogueCmd*)THIS->tokens->array->objects[index])->type->token_type;
  else                             return ROGUE_TOKEN_UNDEFINED;
}

RogueCmd* RogueTokenizer_read( RogueTokenizer* THIS )
{
  return (THIS->tokens->array->objects[THIS->position++]);
}

