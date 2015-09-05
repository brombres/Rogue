//=============================================================================
//  RogueAllocation.c
//
//  2015.09.05 by Abe Pralle
//=============================================================================
#include "Rogue.h"

//-----------------------------------------------------------------------------
//  RogueAllocation
//-----------------------------------------------------------------------------
void* RogueAllocation_create( RogueVM* vm, RogueInteger size )
{
  RogueAllocation* allocation;

  allocation = RogueAllocator_allocate( &vm->allocator, size );
  memset( allocation, 0, size );

  allocation->size = size;

  return allocation;
}

void* RogueAllocation_delete( RogueVM* vm, void* THIS )
{
  RogueAllocator_free( &vm->allocator, THIS, ((RogueAllocation*)THIS)->size );
  return 0;
}

