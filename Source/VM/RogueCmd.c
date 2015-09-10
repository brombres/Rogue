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

RogueCmdType* RogueCmdType_create( RogueVM* vm, RogueCmdID cmd_id,
  const char* name, RogueInteger object_size )
{
  RogueCmdType* cmd_type = (RogueCmdType*) RogueAllocator_allocate( &vm->allocator,
      sizeof(RogueCmdType) );
  cmd_type->vm = vm;
  cmd_type->cmd_id = cmd_id;
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
  switch (((RogueCmd*)THIS)->type->cmd_id)
  {
    case ROGUE_CMD_LITERAL_INTEGER:
      RogueStringBuilder_print_integer( builder, ((RogueCmdLiteralInteger*)THIS)->value );
      break;

    default:;
  }
}

void RogueCmd_init( void* THIS )
{
  switch (((RogueCmd*)THIS)->type->cmd_id)
  {
    default:;
  }
}

void  RogueCmd_throw_error( RogueCmd* THIS, const char* message )
{
  RogueParsePosition position;
  position.line = THIS->line;
  position.column = THIS->column;
  ROGUE_THROW( THIS->type->vm, THIS->filepath, position, message );
}

RogueCmdType* RogueCmd_type( void* THIS )
{
  switch (((RogueCmd*)THIS)->type->cmd_id)
  {
  //return ((RogueCmdUnaryOp*)cmd)->operand->type->type_fn( ((RogueCmdUnaryOp*)cmd)->operand );
    default:
      return 0;
  }
}

void RogueCmdStatementList_init( void* cmd )
{
  RogueCmdStatementList* list = cmd;
  list->statements = RogueVMList_create( list->cmd.type->vm, 5, RogueVMTraceCmd );
}

