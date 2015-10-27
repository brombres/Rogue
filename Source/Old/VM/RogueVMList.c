//=============================================================================
//  RogueVMList.c
//
//  2015.09.05 by Abe Pralle
//=============================================================================
#include "Rogue.h"

RogueVMList* RogueVMList_create( RogueVM* vm, RogueInteger initial_capacity, RogueVMTraceFn trace_fn )
{
  RogueVMList* list = RogueVMObject_create( vm, sizeof(RogueVMList) );
  RogueInteger array_size = sizeof(RogueVMArray) + initial_capacity * sizeof(void*);
  list->vm = vm;
  list->array = RogueVMObject_create( vm, array_size );
  list->capacity = initial_capacity;
  list->trace_fn = trace_fn;
  return list;
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

  {
    RogueInteger array_size = sizeof(RogueVMArray) + required_capacity * sizeof(void*);
    new_array = RogueVMObject_create( THIS->vm, array_size );
    memcpy( new_array->bytes, THIS->array->bytes, THIS->count * sizeof(void*) );
    THIS->array = new_array;
    THIS->capacity = required_capacity;
  }

  return THIS;
}

void RogueVMList_trace( RogueVMList* THIS )
{
  if (THIS->allocation.size >= 0)
  {
    int    count = THIS->count;
    void** data = THIS->array->objects - 1;
    RogueVMTraceFn trace_fn = THIS->trace_fn;

    THIS->allocation.size ^= -1;

    THIS->array->allocation.size ^= -1;

    while (--count >= 0)
    {
      RogueAllocation* cur = *(++data);
      if (cur && cur->size >= 0) trace_fn( cur );
    }
  }
}

