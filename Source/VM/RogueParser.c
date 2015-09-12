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

RogueLogical RogueParser_consume( RogueParser* THIS, RogueCmdID id )
{
  if ( RogueTokenizer_peek_id(THIS->tokenizer,0) != id ) return 0;
  RogueTokenizer_read( THIS->tokenizer );
  return 1;
}

RogueLogical RogueParser_consume_eols( RogueParser* THIS )
{
  RogueLogical success = 0;
  while (RogueParser_consume(THIS,ROGUE_CMD_EOL))
  {
    success = 1;
  }
  return success;
}

void RogueParser_must_consume( RogueParser* THIS, RogueCmdID id, const char* message )
{
  if (RogueParser_consume(THIS,id))
  {
    return;
  }
  else
  {
    void* cmd = RogueTokenizer_peek( THIS->tokenizer, 0 );
    RogueCmd_throw_error( RogueTokenizer_peek(THIS->tokenizer,0), 0 );
    if (message)
    {
      RogueStringBuilder_print_c_string( &THIS->vm->error_message_builder, message );
    }
    else
    {
      RogueStringBuilder_print_c_string( &THIS->vm->error_message_builder, "Expected '" );
      RogueCmd_print( cmd, &THIS->vm->error_message_builder );
      RogueStringBuilder_print_character( &THIS->vm->error_message_builder, '\'' );
      RogueStringBuilder_print_character( &THIS->vm->error_message_builder, '.' );
    }

    RogueCmd_throw_error( RogueTokenizer_peek(THIS->tokenizer,0), 0 );
  }
}

void RogueParser_must_consume_end_cmd( RogueParser* THIS )
{
  if (RogueParser_consume(THIS,ROGUE_CMD_EOL)) return;

  RogueParser_must_consume( THIS, ROGUE_CMD_EOL, "End of line or ';' expected." );
}

void RogueParser_parse_elements( RogueParser* THIS )
{
  RogueTokenizer_tokenize( THIS->tokenizer );
  RogueParser_consume_eols( THIS );

  while (RogueTokenizer_has_another(THIS->tokenizer))
  {
    // Parse global 
    //RogueCmd* cmd = RogueTokenizer_peek( THIS->tokenizer, 0 );

    RogueCmd* statement = RogueParser_parse_statement( THIS );
  }
}

void* RogueParser_parse_statement( RogueParser* THIS )
{
  RogueCmd* expression = RogueParser_parse_expression( THIS );
  RogueParser_must_consume_end_cmd( THIS );
  return expression;
}

void* RogueParser_parse_expression( RogueParser* THIS )
{
  return RogueParser_parse_add_subtract( THIS, 0 );
}

void* RogueParser_parse_add_subtract( RogueParser* THIS, void* left )
{
  void* cmd = RogueTokenizer_peek( THIS->tokenizer, 0 );
  if (left)
  {
    if (RogueParser_consume(THIS,ROGUE_CMD_SYMBOL_PLUS))
    {
      RogueCmdBinaryOp* add_op = cmd;
      add_op->left = left;
      add_op->right = RogueParser_parse_term( THIS );
      return RogueParser_parse_add_subtract( THIS, add_op );
    }
    else
    {
      return left;
    }
  }
  else
  {
    return RogueParser_parse_add_subtract( THIS, RogueParser_parse_term(THIS) );
  }
}

void* RogueParser_parse_term( RogueParser* THIS )
{
  RogueCmd* cmd = RogueTokenizer_read( THIS->tokenizer );

  switch (cmd->type->id)
  {
    case ROGUE_CMD_LITERAL_INTEGER:
      return cmd;
  }

  RogueStringBuilder_print_c_string( &THIS->vm->error_message_builder, "Unexpected '" );
  RogueCmd_print( cmd, &THIS->vm->error_message_builder );
  RogueStringBuilder_print_character( &THIS->vm->error_message_builder, '\'' );
  //RogueStringBuilder_print_c_string( &THIS->vm->error_message_builder, " (ID " );
  //RogueStringBuilder_print_integer( &THIS->vm->error_message_builder, cmd->type->id );
  //RogueStringBuilder_print_c_string( &THIS->vm->error_message_builder, ")" );
  RogueStringBuilder_print_character( &THIS->vm->error_message_builder, '.' );

  RogueCmd_throw_error( cmd, 0 );
  return 0;
}
