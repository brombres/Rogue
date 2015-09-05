//=============================================================================
//  Rogue.c
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
  cmd_type->token_type = token_type;
  if (object_size == -1) object_size = sizeof(RogueCmd);
  return cmd_type;
}

//-----------------------------------------------------------------------------
//  RogueCmd
//-----------------------------------------------------------------------------
void* RogueCmd_create( RogueCmdType* of_type  )
{
  /*
  RogueCmd* object;

  if (size == -1) size = of_type->object_size;
  object = (RogueCmd*) RogueAllocator_allocate( &of_type->vm->allocator, size );
  memset( object, 0, size );

  object->allocation.size = size;
  object->allocation.reference_count = 0;
  object->allocation.next_allocation = (RogueAllocation*) of_type->vm->objects;
  of_type->vm->objects = object;
  object->type = of_type;

  return object;
  */
  return 0;
}

