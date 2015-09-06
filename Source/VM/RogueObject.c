//=============================================================================
//  RogueObject.c
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#include "Rogue.h"

void* RogueObject_create( RogueType* of_type, RogueInteger size )
{
  RogueObject* object;
  RogueVM*     vm = of_type->vm;

  if (size == -1) size = of_type->object_size;

  object = RogueAllocator_allocate( &vm->allocator, size );

  object->type = of_type;
  object->allocation.next_allocation = (RogueAllocation*) vm->objects;
  vm->objects = object;

  return object;
}


void RogueObject_print( void* THIS )
{
  RogueStringBuilder builder;
  RogueStringBuilder_init( &builder, ((RogueObject*)THIS)->type->vm, -1 );

  ((RogueObject*)THIS)->type->print( THIS, &builder );

  RogueStringBuilder_log( &builder );
}

void RogueObject_println( void* THIS )
{
  RogueObject_print( THIS );
  printf( "\n" );
}

