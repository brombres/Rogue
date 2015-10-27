//=============================================================================
//  RogueAllocator.h
//
//  2015.08.27 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_ALLOCATOR_H
#define ROGUE_ALLOCATOR_H

#include "Rogue.h"

//-----------------------------------------------------------------------------
//  RogueAllocationPage
//-----------------------------------------------------------------------------
typedef struct RogueAllocationPage RogueAllocationPage;

struct RogueAllocationPage
{
  // Backs small 0..256-byte allocations.
  RogueAllocationPage* next_page;

  RogueByte* cursor;
  int        remaining;

  RogueByte  data[ ROGUE_MM_PAGE_SIZE ];
};

RogueAllocationPage* RogueAllocationPage_create( RogueAllocationPage* next_page );
void*                RogueAllocationPage_allocate( RogueAllocationPage* THIS, int size );

//-----------------------------------------------------------------------------
//  RogueAllocator
//-----------------------------------------------------------------------------
struct RogueAllocator
{
  RogueVM*             vm;
  RogueAllocationPage* pages;
  RogueAllocation*     free_allocations[ROGUE_MM_SLOT_COUNT];
};

void RogueAllocator_init( RogueVM* vm, RogueAllocator* THIS );
void RogueAllocator_retire( RogueAllocator* THIS );

void* RogueAllocator_allocate( RogueAllocator* THIS, int size );
void* RogueAllocator_free( RogueAllocator* THIS, void* data );

#endif // ROGUE_ALLOCATOR_H
