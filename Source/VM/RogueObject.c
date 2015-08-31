//=============================================================================
//  RogueObject.c
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#include "Rogue.h"

RogueObject* RogueObject_create( RogueType* type, RogueInteger size )
{
  RogueObject* THIS;

  if (size == -1) size = type->object_size;
  THIS = (RogueObject*) RogueAllocator_allocate( &type->vm->allocator, size );

  THIS->allocation.size = size;
  THIS->allocation.reference_count = 0;
  THIS->allocation.next_allocation = (RogueAllocation*) type->vm->objects;
  type->vm->objects = THIS;
  THIS->type = type;

  return THIS;
}

