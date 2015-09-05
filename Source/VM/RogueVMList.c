//=============================================================================
//  RogueVMList.c
//
//  2015.09.05 by Abe Pralle
//=============================================================================
#include "Rogue.h"

RogueVMList* RogueVMList_create( RogueVM* vm, RogueInteger initial_capacity )
{
  RogueVMList* list = RogueAllocator_allocate( &vm->allocator, sizeof(RogueVMList) );
  RogueInteger array_size = sizeof(RogueVMArray) + initial_capacity * sizeof(void*);
  list->array = RogueAllocator_allocate( &vm->allocator, array_size );
  list->capacity = initial_capacity;
  return list;
}

RogueVMList* RogueVMList_delete( RogueVMList* THIS )
{
  RogueAllocator* allocator = &THIS->vm->allocator;
  RogueAllocator_free( allocator, THIS->array );
  RogueAllocator_free( allocator, THIS );
  return THIS;
}


RogueVMList* RogueVMList_add( RogueVMList* THIS, void* object )
{
  RogueVMList_reserve( THIS, 1 );
  THIS->array->objects[ THIS->count++ ] = object;
  return THIS;
}

RogueVMList* RogueVMList_reserve( RogueVMList* THIS, RogueInteger additional_capacity )
{
  RogueInteger required_capacity = THIS->count + additional_capacity;
  RogueInteger double_capacity;
  RogueVMArray* new_array;

  if (required_capacity <= THIS->capacity) return THIS;

  double_capacity = THIS->capacity << 1;
  if (double_capacity > required_capacity) required_capacity = double_capacity;

printf( "new capacity %d\n", required_capacity );
  {
    RogueAllocator* allocator = &THIS->vm->allocator;
    RogueInteger array_size = sizeof(RogueVMArray) + required_capacity * sizeof(void*);
    new_array = RogueAllocator_allocate( allocator, array_size );
    memcpy( new_array->bytes, THIS->array->bytes, THIS->count * sizeof(void*) );
    RogueAllocator_free( allocator, THIS->array );
    THIS->array = new_array;
    THIS->capacity = required_capacity;
  }

  return THIS;
}

