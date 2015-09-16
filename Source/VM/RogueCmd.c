//=============================================================================
//  RogueCmd.c
//
//  2015.08.29 by Abe Pralle
//=============================================================================
#include "Rogue.h"

//-----------------------------------------------------------------------------
//  RogueCmd
//-----------------------------------------------------------------------------
void* RogueCmd_create( RogueVM* vm, RogueCmdType cmd_type, size_t object_size )
{
  RogueCmd* cmd = RogueVMObject_create( vm, (RogueInteger) object_size );

  cmd->vm   = vm;
  cmd->cmd_type = cmd_type;

  return cmd;
}

RogueCmdBinaryOp* RogueCmdBinaryOp_create( RogueVM* vm, RogueCmdType cmd_type, RogueCmd* left, RogueCmd* right )
{
  RogueCmdBinaryOp* cmd = RogueCmd_create( vm, cmd_type, sizeof(RogueCmdBinaryOp) );
  cmd->left = left;
  cmd->right = right;
  return cmd;
}

RogueCmdLiteralInteger* RogueCmdLiteralInteger_create( RogueVM* vm, RogueInteger value )
{
  RogueCmdLiteralInteger* cmd = RogueCmd_create( vm, ROGUE_CMD_LITERAL_INTEGER, sizeof(RogueCmdLiteralInteger) );
  cmd->value = value;
  return cmd;
}

RogueCmdLiteralString* RogueCmdLiteralString_create( RogueVM* vm, RogueString* value )
{
  RogueCmdLiteralString* cmd = RogueCmd_create( vm, ROGUE_CMD_LITERAL_STRING, sizeof(RogueCmdLiteralString) );
  cmd->value = value;
  return cmd;
}

RogueCmdUnaryOp*  RogueCmdUnaryOp_create( RogueVM* vm, RogueCmdType cmd_type, RogueCmd* operand )
{
  RogueCmdUnaryOp* cmd = RogueCmd_create( vm, cmd_type, sizeof(RogueCmdUnaryOp) );
  cmd->operand = operand;
  return cmd;
}

void RogueCmd_print( void* THIS, RogueStringBuilder* builder )
{
  switch (((RogueCmd*)THIS)->cmd_type)
  {
    /*
    case ROGUE_CMD_LITERAL_INTEGER:
      RogueStringBuilder_print_integer( builder, ((RogueCmdLiteralInteger*)THIS)->value );
      break;
      */

    default:
      RogueStringBuilder_print_c_string( builder, "(Unnamed Cmd)" );
  }
}

void  RogueCmd_throw_error( RogueCmd* THIS, const char* message )
{
  ROGUE_THROW( THIS->vm, message );
}

void RogueCmd_trace( void* THIS )
{
  if ( !THIS || ((RogueCmd*)THIS)->allocation.size < 0 ) return;
  ((RogueCmd*)THIS)->allocation.size ^= -1;

  switch (((RogueCmd*)THIS)->cmd_type)
  {
    /*
    case ROGUE_CMD_SYMBOL_EQ:
    case ROGUE_CMD_SYMBOL_EQUALS:
    case ROGUE_CMD_SYMBOL_GE:
    case ROGUE_CMD_SYMBOL_GT:
    case ROGUE_CMD_SYMBOL_LE:
    case ROGUE_CMD_SYMBOL_LT:
    case ROGUE_CMD_SYMBOL_NE:
    case ROGUE_CMD_SYMBOL_PLUS:
      RogueCmd_trace( ((RogueCmdBinaryOp*)THIS)->left );
      RogueCmd_trace( ((RogueCmdBinaryOp*)THIS)->right );
      break;

    case ROGUE_CMD_LIST:
      RogueVMList_trace( ((RogueCmdList*)THIS)->statements );
      break;
      */

    default:;
  }
}

RogueCmdList* RogueCmdList_create( RogueVM* vm, RogueInteger initial_capacity )
{
  RogueCmdList* list = RogueCmd_create( vm, ROGUE_CMD_LIST, sizeof(RogueCmdList) );
  list->statements = RogueVMList_create( vm, initial_capacity, RogueVMTraceCmd );
  return list;
}

RogueType* RogueCmd_type( void* THIS )
{
  RogueVM* vm = ((RogueCmd*)THIS)->vm;
  switch (((RogueCmd*)THIS)->cmd_type)
  {
    case ROGUE_CMD_LITERAL_INTEGER:
      return vm->type_Integer;

    case ROGUE_CMD_LITERAL_STRING:
      return vm->type_String;

    default:
      return 0;
  }
}

void* RogueCmd_resolve( void* THIS )
{
  //switch (((RogueCmd*)THIS)->cmd_type)
  //{
  //  case RogueCmd
  //}

  return THIS;
}

void RogueCmd_execute( void* THIS )
{
  switch (((RogueCmd*)THIS)->cmd_type)
  {
    case ROGUE_CMD_LIST:
    {
      RogueInteger count = ((RogueCmdList*)THIS)->statements->count;
      RogueCmd** cur = (RogueCmd**) (((RogueCmdList*)THIS)->statements->array->objects - 1);

      while (--count >= 0)
      {
        RogueCmd_execute( *(++cur) );
      }
      break;
    }

    case ROGUE_CMD_LOG_INTEGER:
      printf( "%d\n", RogueCmd_execute_integer_op(((RogueCmdUnaryOp*)THIS)->operand) );
      break;

    case ROGUE_CMD_LOG_STRING:
      RogueString_log( RogueCmd_execute_string_op(((RogueCmdUnaryOp*)THIS)->operand) );
      printf( "\n" );
      break;

    default:
    {
      RogueVM* vm = ((RogueCmd*)THIS)->vm;
      RogueStringBuilder_print_c_string( &vm->error_message_builder,
          "[INTERNAL] RogueCmd_execute() not defined for Cmd type " );
      RogueStringBuilder_print_integer( &vm->error_message_builder, ((RogueCmd*)THIS)->cmd_type );
      ROGUE_THROW( ((RogueCmd*)THIS)->vm, "." );
    }
  }
}

RogueInteger RogueCmd_execute_integer_op( void* THIS )
{
  switch (((RogueCmd*)THIS)->cmd_type)
  {
    case ROGUE_CMD_LITERAL_INTEGER:
      return ((RogueCmdLiteralInteger*)THIS)->value;

    default:
    {
      RogueVM* vm = ((RogueCmd*)THIS)->vm;
      RogueStringBuilder_print_c_string( &vm->error_message_builder,
          "[INTERNAL] RogueCmd_execute_string_op() not defined for Cmd type " );
      RogueStringBuilder_print_integer( &vm->error_message_builder, ((RogueCmd*)THIS)->cmd_type );
      ROGUE_THROW( ((RogueCmd*)THIS)->vm, "." );
    }
  }
}

RogueString* RogueCmd_execute_string_op( void* THIS )
{
  switch (((RogueCmd*)THIS)->cmd_type)
  {
    case ROGUE_CMD_LITERAL_STRING:
      return ((RogueCmdLiteralString*)THIS)->value;

    default:
    {
      RogueVM* vm = ((RogueCmd*)THIS)->vm;
      RogueStringBuilder_print_c_string( &vm->error_message_builder,
          "[INTERNAL] RogueCmd_execute_string_op() not defined for Cmd type " );
      RogueStringBuilder_print_integer( &vm->error_message_builder, ((RogueCmd*)THIS)->cmd_type );
      ROGUE_THROW( ((RogueCmd*)THIS)->vm, "." );
    }
  }
}
