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

RogueCmdList* RogueCmdList_create( RogueVM* vm, RogueInteger initial_capacity )
{
  RogueCmdList* list = RogueCmd_create( vm, ROGUE_CMD_LIST, sizeof(RogueCmdList) );
  list->statements = RogueVMList_create( vm, initial_capacity, RogueVMTraceCmd );
  return list;
}

RogueCmdLiteralInteger* RogueCmdLiteralInteger_create( RogueVM* vm, RogueInteger value )
{
  RogueCmdLiteralInteger* cmd = RogueCmd_create( vm, ROGUE_CMD_LITERAL_INTEGER, sizeof(RogueCmdLiteralInteger) );
  cmd->value = value;
  return cmd;
}

RogueCmdLiteralReal* RogueCmdLiteralReal_create( RogueVM* vm, RogueReal value )
{
  RogueCmdLiteralReal* cmd = RogueCmd_create( vm, ROGUE_CMD_LITERAL_REAL, sizeof(RogueCmdLiteralReal) );
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

RogueType* RogueCmd_require_common_binary_op_type( void* THIS )
{
  RogueType* left_type  = RogueCmd_require_type( ((RogueCmdBinaryOp*)THIS)->left );
  RogueType* right_type = RogueCmd_require_type( ((RogueCmdBinaryOp*)THIS)->right );

  if (left_type != right_type)
  {
    RogueVM* vm = ((RogueCmd*)THIS)->vm;
    RogueStringBuilder_print_c_string( &vm->error_message_builder, "Mismatched operand types for command type " );
    RogueStringBuilder_print_integer( &vm->error_message_builder, ((RogueCmd*)THIS)->cmd_type );
    ROGUE_THROW( ((RogueCmd*)THIS)->vm, "." );
  }

  return left_type;
}

RogueType* RogueCmd_require_type( void* THIS )
{
  RogueType* type = RogueCmd_type( THIS );

  if ( !type )
  {
    RogueVM* vm = ((RogueCmd*)THIS)->vm;
    RogueStringBuilder_print_c_string( &vm->error_message_builder, "Value expected for command type " );
    RogueStringBuilder_print_integer( &vm->error_message_builder, ((RogueCmd*)THIS)->cmd_type );
    ROGUE_THROW( ((RogueCmd*)THIS)->vm, "." );
  }

  return type;
}

void* RogueCmd_require_value( void* THIS )
{
  RogueCmd_require_type( THIS );
  return THIS;
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

RogueType* RogueCmd_type( void* THIS )
{
  RogueVM* vm = ((RogueCmd*)THIS)->vm;
  switch (((RogueCmd*)THIS)->cmd_type)
  {
    case ROGUE_CMD_LITERAL_INTEGER:
    case ROGUE_CMD_ADD_INTEGER:
      return vm->type_Integer;

    case ROGUE_CMD_LITERAL_REAL:
    case ROGUE_CMD_ADD_REAL:
      return vm->type_Real;

    case ROGUE_CMD_LITERAL_STRING:
      return vm->type_String;

    default:
      return 0;
  }
}

void* RogueCmd_resolve( void* THIS )
{
  RogueVM* vm = ((RogueCmd*)THIS)->vm;
  for (;;)
  {
    switch (((RogueCmd*)THIS)->cmd_type)
    {
      case ROGUE_CMD_LIST:
      {
        RogueInteger i;
        RogueVMList*  statements = ((RogueCmdList*)THIS)->statements;
        RogueVMArray* array = statements->array;
        RogueInteger count = statements->count;
        for (i=0; i<count; ++i)
        {
          array->objects[i] = RogueCmd_resolve( array->objects[i] );
        }
        return THIS;
      }

      case ROGUE_CMD_LOG_VALUE:
      {
        RogueCmdUnaryOp* op = THIS;
        op->operand = RogueCmd_require_value( RogueCmd_resolve(op->operand) );

        RogueType* operand_type = RogueCmd_type( op->operand );
        if (operand_type == vm->type_String)
        {
          ((RogueCmd*)THIS)->cmd_type = ROGUE_CMD_LOG_STRING;
        }
        else
        {
          RogueCmd_refine_for_common_type( THIS, operand_type,
              ROGUE_CMD_LOG_REAL, ROGUE_CMD_LOG_INTEGER, 1 );
        }
        return THIS;
      }

      case ROGUE_CMD_LITERAL_INTEGER:
      case ROGUE_CMD_LITERAL_REAL:
      case ROGUE_CMD_LITERAL_STRING:
        return THIS;

      case ROGUE_CMD_ADD:
      {
        RogueType* operand_type;
        RogueCmdBinaryOp* op = THIS;
        op->left  = RogueCmd_require_value( RogueCmd_resolve(op->left) );
        op->right = RogueCmd_require_value( RogueCmd_resolve(op->right) );

        operand_type = RogueCmd_require_common_binary_op_type( op );

        RogueCmd_refine_for_common_type( THIS, operand_type,
            ROGUE_CMD_ADD_REAL, ROGUE_CMD_ADD_INTEGER, 1 );
        return THIS;
      }

      default:
      {
        RogueStringBuilder_print_c_string( &vm->error_message_builder,
            "[INTERNAL] RogueCmd_resolve() not defined for Cmd type " );
        RogueStringBuilder_print_integer( &vm->error_message_builder, ((RogueCmd*)THIS)->cmd_type );
        ROGUE_THROW( vm, "." );
      }
    }
    break;
  }

  return THIS;
}

RogueLogical RogueCmd_refine_for_common_type( void* THIS, RogueType* type,
       RogueCmdType for_real, RogueCmdType for_integer, RogueLogical must_resolve )
{
  RogueVM* vm = ((RogueCmd*)THIS)->vm;

  if (type == ((RogueCmd*)THIS)->vm->type_Real)         ((RogueCmd*)THIS)->cmd_type = for_real;
  else if (type == ((RogueCmd*)THIS)->vm->type_Integer) ((RogueCmd*)THIS)->cmd_type = for_integer;
  else if (must_resolve)
  {
    RogueStringBuilder_print_c_string( &vm->error_message_builder, "Invalid operand type " );
    if (type)
    {
      RogueStringBuilder_print_c_string( &vm->error_message_builder, type->name );
      RogueStringBuilder_print_character( &vm->error_message_builder, ' ' );
    }
    RogueStringBuilder_print_c_string( &vm->error_message_builder, "for command " );
    RogueStringBuilder_print_integer( &vm->error_message_builder, ((RogueCmd*)THIS)->cmd_type );
    ROGUE_THROW( ((RogueCmd*)THIS)->vm, "." );
  }
  else return 0;

  return 1;
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

    case ROGUE_CMD_LOG_REAL:
      printf( "%lf\n", RogueCmd_execute_real_op(((RogueCmdUnaryOp*)THIS)->operand) );
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

    case ROGUE_CMD_LITERAL_REAL:
      return (RogueInteger)(((RogueCmdLiteralReal*)THIS)->value);

    case ROGUE_CMD_ADD_INTEGER:
      return RogueCmd_execute_integer_op( ((RogueCmdBinaryOp*)THIS)->left ) +
             RogueCmd_execute_integer_op( ((RogueCmdBinaryOp*)THIS)->right );

    case ROGUE_CMD_ADD_REAL:
      return (RogueInteger)
             RogueCmd_execute_real_op( ((RogueCmdBinaryOp*)THIS)->left ) +
             RogueCmd_execute_real_op( ((RogueCmdBinaryOp*)THIS)->right );

    default:
    {
      RogueVM* vm = ((RogueCmd*)THIS)->vm;
      RogueStringBuilder_print_c_string( &vm->error_message_builder,
          "[INTERNAL] RogueCmd_execute_integer_op() not defined for Cmd type " );
      RogueStringBuilder_print_integer( &vm->error_message_builder, ((RogueCmd*)THIS)->cmd_type );
      ROGUE_THROW( ((RogueCmd*)THIS)->vm, "." );
    }
  }
}

RogueReal RogueCmd_execute_real_op( void* THIS )
{
  switch (((RogueCmd*)THIS)->cmd_type)
  {
    case ROGUE_CMD_LITERAL_INTEGER:
      return ((RogueCmdLiteralReal*)THIS)->value;

    case ROGUE_CMD_LITERAL_REAL:
      return ((RogueCmdLiteralReal*)THIS)->value;

    case ROGUE_CMD_ADD_INTEGER:
      return RogueCmd_execute_integer_op( ((RogueCmdBinaryOp*)THIS)->left ) +
             RogueCmd_execute_integer_op( ((RogueCmdBinaryOp*)THIS)->right );

    case ROGUE_CMD_ADD_REAL:
      return RogueCmd_execute_real_op( ((RogueCmdBinaryOp*)THIS)->left ) +
             RogueCmd_execute_real_op( ((RogueCmdBinaryOp*)THIS)->right );

    default:
    {
      RogueVM* vm = ((RogueCmd*)THIS)->vm;
      RogueStringBuilder_print_c_string( &vm->error_message_builder,
          "[INTERNAL] RogueCmd_execute_real_op() not defined for Cmd type " );
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
