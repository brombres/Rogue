//=============================================================================
//  RogueObject.c
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#include "Rogue.h"

void* RogueObject_create( RogueType* of_type, RogueInteger size )
{
  RogueObject* object;

  if (size == -1) size = of_type->object_size;
  object = (RogueObject*) RogueAllocator_allocate( &of_type->vm->allocator, size );
  memset( object, 0, size );

  object->allocation.size = size;
  object->allocation.reference_count = 0;
  object->allocation.next_allocation = (RogueAllocation*) of_type->vm->objects;
  of_type->vm->objects = object;
  object->type = of_type;

  return object;
}


void RogueObject_print( void* THIS )
{
  RogueStringBuilder builder;
  RogueStringBuilder_init( &builder, -1 );

  ((RogueObject*)THIS)->type->print( THIS, &builder );

  RogueStringBuilder_log( &builder );
}

void RogueObject_println( void* THIS )
{
  RogueObject_print( THIS );
  printf( "\n" );
}

