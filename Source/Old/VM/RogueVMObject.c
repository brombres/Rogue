//=============================================================================
//  RogueVMObject.c
//
//  2015.09.08 by Abe Pralle
//=============================================================================
#include "Rogue.h"

void RogueVMTraceCmd( void* allocation )
{
  printf( "TODO: RogueVMTraceCmd()\n" );
}

void RogueVMTraceMethod( void* allocation )
{
  printf( "TODO: RogueVMTraceMethod()\n" );
}

void RogueVMTraceType( void* allocation )
{
  RogueType_trace( allocation );
}

void* RogueVMObject_create( RogueVM* vm, RogueInteger size )
{
  RogueAllocation* object = RogueAllocator_allocate( &vm->allocator, size ); 

  object->next_allocation = vm->vm_objects;
  vm->vm_objects = object;

  vm->have_new_vm_objects = 1;

  return object;
}
