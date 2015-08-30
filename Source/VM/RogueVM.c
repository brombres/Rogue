//=============================================================================
//  RogueVM.c
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#include "Rogue.h"

RogueVM* RogueVM_create()
{
  RogueVM* THIS = (RogueVM*) malloc( sizeof(RogueVM) );
  RogueAllocator_init( &THIS->allocator );
  return THIS;
}

RogueVM* RogueVM_delete( RogueVM* THIS )
{
  if (THIS)
  {
    RogueAllocator_retire( &THIS->allocator );
    free( THIS );
  }
  return 0;
}

