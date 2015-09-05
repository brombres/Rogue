//=============================================================================
//  RogueVMList.c
//
//  2015.09.05 by Abe Pralle
//=============================================================================
#include "Rogue.h"

RogueVMList* RogueVMList_create( RogueVM* vm, RogueInteger initial_capacity )
{
  RogueVMList* list = RogueAllocation_create( vm, sizeof(RogueVMList) );
  RogueInteger array_size = sizeof(RogueVMArray) + initial_capacity * sizeof(void*);
  list->array = RogueAllocation_create( vm, array_size );
  list->capacity = initial_capacity;
  return list;
}

RogueVMList* RogueVMList_delete( RogueVMList* THIS )
{
  RogueAllocation_delete( THIS->vm, THIS->array );
  RogueAllocation_delete( THIS->vm, THIS );
  return THIS;
}


RogueVMList* RogueVMList_add_object( RogueVMList* THIS, void* object )
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

  {
    RogueInteger array_size = sizeof(RogueVMArray) + required_capacity * sizeof(void*);
    new_array = RogueAllocation_create( THIS->vm, array_size );
    memcpy( new_array->bytes, THIS->array->bytes, THIS->count * sizeof(void*) );
    RogueAllocation_delete( THIS->vm, THIS->array );
    THIS->array = new_array;
  }

  return THIS;
}

