//=============================================================================
//  RogueCmd.c
//
//  2015.08.29 by Abe Pralle
//=============================================================================
#include "Rogue.h"

//-----------------------------------------------------------------------------
//  RogueCmdType
//-----------------------------------------------------------------------------
RogueCmdType* RogueCmdType_create( RogueVM* vm, RogueTokenType token_type,
  RogueInteger object_size )
{
  RogueCmdType* cmd_type = (RogueCmdType*) RogueAllocator_allocate( &vm->allocator,
      sizeof(RogueCmdType) );
  cmd_type->vm = vm;
  cmd_type->token_type = token_type;
  cmd_type->object_size = object_size;
  return cmd_type;
}

//-----------------------------------------------------------------------------
//  RogueCmd
//-----------------------------------------------------------------------------
void* RogueCmd_create( RogueCmdType* of_type  )
{
  RogueCmd* cmd;
  RogueVM* vm = of_type->vm;

  cmd = (RogueCmd*) RogueAllocator_allocate( &vm->allocator, of_type->object_size );

  cmd->type = of_type;
  cmd->allocation.next_allocation = vm->cmd_objects;
  vm->cmd_objects = cmd;

  return cmd;
}

