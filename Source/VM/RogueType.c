//=============================================================================
//  RogueType.c
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#include "Rogue.h"

RogueType* RogueType_create( RogueVM* vm, const char* name, RogueInteger object_size )
{
  int len = strlen( name );

  RogueType* THIS = (RogueType*) malloc( sizeof(RogueType) );
  memset( THIS, 0, sizeof(RogueType) );
  THIS->vm = vm;

  THIS->name = (char*) malloc( len+1 );
  strcpy( THIS->name, name );

  THIS->object_size = object_size;

  return THIS;
}

RogueType* RogueType_delete( RogueType* THIS )
{
  if (THIS)
  {
    free( THIS->name );
    free( THIS );
  }
  return 0;
}

RogueObject* RogueType_create_object( RogueType* THIS, RogueInteger size )
{
  RogueObject* object;

  if (size == -1) size = THIS->object_size;
  object = (RogueObject*) RogueAllocator_allocate( &THIS->vm->allocator, size );
  memset( object, 0, size );

  object->allocation.size = size;
  object->allocation.reference_count = 0;
  object->allocation.next_allocation = (RogueAllocation*) THIS->vm->objects;
  THIS->vm->objects = object;
  object->type = THIS;

  return object;
}

