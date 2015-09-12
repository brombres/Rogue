//=============================================================================
//  RogueVMArray.c
//
//  2015.09.11 by Abe Pralle
//=============================================================================
#include "Rogue.h"

RogueVMArray* RogueVMArray_create( RogueVM* vm, RogueInteger element_size, RogueInteger count )
{
  RogueVMArray* array = RogueAllocator_allocate( &vm->allocator, sizeof(RogueVMArray)+element_size*count );
  array->element_size = element_size;
  array->count = count;
  return array;
}
