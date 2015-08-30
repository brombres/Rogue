//=============================================================================
//  RogueAllocator.c
//
//  2015.08.29 by Abe Pralle
//=============================================================================
#include "Rogue.h"

//-----------------------------------------------------------------------------
//  RogueAllocationPage
//-----------------------------------------------------------------------------
RogueAllocationPage* RogueAllocationPage_create( RogueAllocationPage* next_page )
{
  RogueAllocationPage* THIS = (RogueAllocationPage*) malloc( sizeof(RogueAllocationPage) );
  THIS->cursor = THIS->data;
  THIS->next_page = next_page;
  THIS->remaining = ROGUE_MM_PAGE_SIZE;
  return THIS;
}

void* RogueAllocationPage_allocate( RogueAllocationPage* THIS, int size )
{
  void* result;

  // Round size up to multiple of 8.
  if (size > 0) size = (size + 7) & ~7;
  else          size = 8;

  if (size > THIS->remaining) return 0;

  //printf( "Allocating %d bytes from page.\n", size );
  result = THIS->cursor;
  THIS->cursor    += size;
  THIS->remaining -= size;

  //printf( "%d / %d\n", ROGUE_MM_PAGE_SIZE - remaining, ROGUE_MM_PAGE_SIZE );
  return result;
}


//-----------------------------------------------------------------------------
//  RogueAllocator
//-----------------------------------------------------------------------------
void RogueAllocator_init( RogueAllocator* THIS )
{
  int i;
  THIS->pages = 0;
  for (i=0; i<ROGUE_MM_SLOT_COUNT; ++i)
  {
    THIS->free_objects[i] = NULL;
  }
}

void RogueAllocator_retire( RogueAllocator* THIS )
{
  while (THIS->pages)
  {
    RogueAllocationPage* next_page = THIS->pages->next_page;
    free( THIS->pages );
    THIS->pages = next_page;
  }
}

void* RogueAllocator_allocate( RogueAllocator* THIS, int size )
{
  int slot, s;
  RogueAllocation* obj;

  if (size > ROGUE_MM_SMALL_ALLOCATION_SIZE_LIMIT) return malloc( size );

  if (size <= 0) size = ROGUE_MM_GRANULARITY_SIZE;
  else           size = (size + ROGUE_MM_GRANULARITY_MASK) & ~ROGUE_MM_GRANULARITY_MASK;

  slot = (size >> ROGUE_MM_GRANULARITY_BITS);
  obj = THIS->free_objects[slot];
  
  if (obj)
  {
    //printf( "found free object\n");
    THIS->free_objects[slot] = obj->next_allocation;
    return obj;
  }

  // No free objects for requested size.

  // Try allocating a new object from the current page.
  if ( !THIS->pages )
  {
    THIS->pages = RogueAllocationPage_create(0);
  }

  obj = (RogueAllocation*) RogueAllocationPage_allocate( THIS->pages, size );
  if (obj) return obj;


  // Not enough room on allocation page.  Allocate any smaller blocks
  // we're able to and then move on to a new page.
  s = slot - 1;
  while (s >= 1)
  {
    obj = (RogueAllocation*) RogueAllocationPage_allocate( THIS->pages, s << ROGUE_MM_GRANULARITY_BITS );
    if (obj)
    {
      //printf( "free obj size %d\n", (s << ROGUE_MM_GRANULARITY_BITS) );
      obj->next_allocation = THIS->free_objects[s];
      THIS->free_objects[s] = obj;
    }
    else
    {
      --s;
    }
  }

  // New page; this will work for sure.
  THIS->pages = RogueAllocationPage_create( THIS->pages );
  return RogueAllocationPage_allocate( THIS->pages, size );
}


void* RogueAllocator_free( RogueAllocator* THIS, void* data, int size )
{
  if (data)
  {
    if (size > ROGUE_MM_SMALL_ALLOCATION_SIZE_LIMIT)
    {
      free( data );
    }
    else
    {
      // Return object to small allocation pool
      RogueAllocation* allocation = (RogueAllocation*) data;
      int slot = (size + ROGUE_MM_GRANULARITY_MASK) >> ROGUE_MM_GRANULARITY_BITS;
      if (slot <= 0) slot = 1;
      allocation->next_allocation = THIS->free_objects[slot];
      THIS->free_objects[slot] = allocation;
    }
  }

  // Always returns null, allowing a pointer to be freed and assigned null in
  // a single step.
  return 0;
}

