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
  RogueStringBuilder_init( &tokenizer->builder, vm, -1 );
  return tokenizer;
}

RogueTokenizer* RogueTokenizer_delete( RogueTokenizer* THIS )
{
  RogueAllocator_free( &THIS->vm->allocator, THIS );
  return 0;
}

void RogueTokenizer_add_token( RogueTokenizer* THIS, void* token )
{
  ((RogueCmd*)token)->filepath = THIS->reader->filepath;
  ((RogueCmd*)token)->line = THIS->parse_position.line;
  ((RogueCmd*)token)->column = THIS->parse_position.column;
  RogueVMList_add( THIS->tokens, token );
}

RogueLogical RogueTokenizer_next_is_real( RogueTokenizer* THIS )
{
  RogueInteger   lookahead = 0;
  RogueCharacter ch = RogueParseReader_peek( THIS->reader, lookahead );
  while (ch >= '0' && ch <'9')
  {
    ch = RogueParseReader_peek( THIS->reader, ++lookahead );
  }
  if (ch == 'e' || ch == 'E') return 1;
  if (ch != '.') return 0;
  ch = RogueParseReader_peek( THIS->reader, ++lookahead );
  return (ch >= '0' && ch <= '9');
}

RogueLogical RogueTokenizer_tokenize( RogueTokenizer* THIS )
{
  if ( !THIS->tokens ) THIS->tokens = RogueVMList_create( THIS->vm, 20, RogueVMTraceCmd );
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

  THIS->parse_position = THIS->reader->position;
  ch = RogueParseReader_peek( THIS->reader, 0 );
  if (ch == '\n')
  {
    RogueParseReader_read( THIS->reader );
    RogueTokenizer_add_token( THIS, RogueCmd_create(THIS->vm->cmd_type_eol) );
    return 1;
  }

  if (ch >= '0' && ch <= '9')
  {
    RogueTokenizer_tokenize_integer_or_long( THIS, 10 );
    return 1;
  }
  
  {
    RogueCmdType* type = RogueTokenizer_tokenize_symbol( THIS );
    if (type)
    {
      RogueTokenizer_add_token( THIS, RogueCmd_create(type) );
    }
    // else next token was parsed (otherwise exception would be thrown), but the
    // method handled adding it to the token list since it required special handling.
    return 1;
  }

  return 0;
}

void RogueTokenizer_tokenize_integer_or_long( RogueTokenizer* THIS, RogueInteger base )
{
  RogueLong value = 0;

  while (RogueParseReader_next_is_digit(THIS->reader, base))
  {
    value = value * base + RogueParseReader_read_digit( THIS->reader );
  }

  {
    RogueCmdLiteralInteger* cmd = RogueCmd_create( THIS->vm->cmd_type_literal_integer );
    cmd->value = value;
    RogueTokenizer_add_token( THIS, cmd );
  }
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
    case '(':
      return vm->cmd_type_symbol_open_paren;
    case ')':
      return vm->cmd_type_symbol_close_paren;
    case '+':
      return vm->cmd_type_symbol_plus;
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

RogueCmdID RogueTokenizer_peek_id( RogueTokenizer* THIS, RogueInteger lookahead )
{
  RogueInteger index = THIS->position + lookahead;
  if (index < THIS->tokens->count) return ((RogueCmd*)THIS->tokens->array->objects[index])->type->id;
  else                             return ROGUE_CMD_UNDEFINED;
}

RogueCmd* RogueTokenizer_read( RogueTokenizer* THIS )
{
  return (THIS->tokens->array->objects[THIS->position++]);
}

