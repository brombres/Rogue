//=============================================================================
//  RogueCmd.c
//
//  2015.08.29 by Abe Pralle
//=============================================================================
#include "Rogue.h"

//-----------------------------------------------------------------------------
//  RogueCmdType
//-----------------------------------------------------------------------------
void RogueCmdPrintFn_default( void* cmd, RogueStringBuilder* builder )
{
  RogueStringBuilder_print_c_string( builder, ((RogueCmd*)cmd)->type->name );
}

RogueType* RogueCmdTypeFn_default( void* cmd )
{
  return 0;
}

RogueType* RogueCmdTypeFn_integer( void* cmd )
{
  return ((RogueCmd*)cmd)->type->vm->type_Integer;
}

RogueCmdType* RogueCmdType_create( RogueVM* vm, RogueCmdID id,
  const char* name, RogueInteger object_size )
{
  RogueCmdType* cmd_type = (RogueCmdType*) RogueAllocator_allocate( &vm->allocator,
      sizeof(RogueCmdType) );
  cmd_type->vm = vm;
  cmd_type->id = id;
  cmd_type->object_size = object_size;
  cmd_type->name = name;
  return cmd_type;
}


//-----------------------------------------------------------------------------
//  RogueCmd
//-----------------------------------------------------------------------------
void* RogueCmd_create( RogueCmdType* of_type  )
{
  RogueCmd* cmd;
  RogueVM* vm = of_type->vm;

  cmd = RogueVMObject_create( vm, of_type->object_size );

  cmd->type = of_type;
  RogueCmd_init( cmd );

  return cmd;
}

void RogueCmd_print( void* THIS, RogueStringBuilder* builder )
{
  switch (((RogueCmd*)THIS)->type->id)
  {
    case ROGUE_CMD_LITERAL_INTEGER:
      RogueStringBuilder_print_integer( builder, ((RogueCmdLiteralInteger*)THIS)->value );
      break;

    default:
      RogueStringBuilder_print_c_string( builder, ((RogueCmd*)THIS)->type->name );
  }
}

void RogueCmd_init( void* THIS )
{
  switch (((RogueCmd*)THIS)->type->id)
  {
    default:;
  }
}

void  RogueCmd_throw_error( RogueCmd* THIS, const char* message )
{
  ROGUE_THROW( THIS->type->vm, message );
}

void RogueCmd_trace( void* THIS )
{
  if ( !THIS || ((RogueCmd*)THIS)->allocation.size < 0 ) return;
  ((RogueCmd*)THIS)->allocation.size ^= -1;

  switch (((RogueCmd*)THIS)->type->id)
  {
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

    case ROGUE_CMD_STATEMENT_LIST:
      RogueVMList_trace( ((RogueCmdStatementList*)THIS)->statements );
      break;

    default:;
  }
}

RogueCmdType* RogueCmd_type( void* THIS )
{
  switch (((RogueCmd*)THIS)->type->id)
  {
  //return ((RogueCmdUnaryOp*)cmd)->operand->type->type_fn( ((RogueCmdUnaryOp*)cmd)->operand );
    default:
      return 0;
  }
}

RogueCmdStatementList* RogueCmdStatementList_create( RogueVM* vm )
{
  RogueCmdStatementList* list = RogueCmd_create( vm->cmd_type_statement_list );
  list->statements = RogueVMList_create( list->cmd.type->vm, 5, RogueVMTraceCmd );
  return list;
}

