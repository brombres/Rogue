//=============================================================================
//  RogueVMObject.c
//
//  2015.09.08 by Abe Pralle
//=============================================================================
#include "Rogue.h"

void* RogueVMObject_create( RogueVM* vm, RogueInteger size )
{
  RogueAllocation* object = RogueAllocator_allocate( &vm->allocator, size ); 

  object->next_allocation = vm->vm_objects;
  vm->vm_objects = object;

  return object;
}
