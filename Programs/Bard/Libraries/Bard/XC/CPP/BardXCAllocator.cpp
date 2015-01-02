#include <stdlib.h>
#include <string.h>


#ifndef BARD_XC_ALLOCATE
#  define BARD_XC_ALLOCATE(bytes) malloc(bytes)
#endif

#ifndef BARD_XC_FREE
#  define BARD_XC_FREE(ptr) free(ptr)
#endif

#include "BardXCAllocator.h"

#ifndef BARD_XC_ALLOCATOR_PAGE_SIZE
#  define BARD_XC_ALLOCATOR_PAGE_SIZE (128*1024)
#endif

enum
{
  BARD_XC_ALLOCATOR_SLOT_COUNT               =  17,
    // - The slot index times 16 is the allocation size of that slot.
    // - Last slot is unlimited size (regular allocation)
    // - Not all slots are used - allocator restricts values to maximize block
    //   reuse for varying sizes.  Only sizes 16, 32, 48, 64, (small allocations)
    //   and 128, 192, 256 (medium allocations) are used.
  BARD_XC_ALLOCATOR_SMALL_OBJECT_BITS        =   4,
  BARD_XC_ALLOCATOR_SMALL_OBJECT_SIZE_LIMIT  = ((BARD_XC_ALLOCATOR_SLOT_COUNT-1) << BARD_XC_ALLOCATOR_SMALL_OBJECT_BITS)
};



//=============================================================================
// GLOBALS
//=============================================================================
static int BardXCAllocator_byte_size_to_slot_size[] =
{
  // Size 0, 1..16  -> 16 bytes
  16, 
  16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,

  // Size 17..32 -> 32 bytes
  32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,

  // Size 33..48 -> 48 bytes
  48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48,

  // Size 49..63 -> 64 bytes
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,

  // Size 65..128 -> 128 bytes
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
  128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,

  // Size 129..192 -> 192 bytes
  192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192,
  192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192,
  192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192,
  192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192,

  // Size 193..256 -> 256 bytes
  256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256,
  256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256,
  256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256,
  256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256,
};


//=============================================================================
//  BardMMAllocation
//=============================================================================
typedef struct BardMMAllocation
{
  struct BardMMAllocation* next_allocation;
  struct BardMMAllocation* next_reference;
} BardMMAllocation;


//=============================================================================
// BardMMAllocationPage
//=============================================================================
typedef struct BardMMAllocationPage
{
  struct BardMMAllocationPage* next_page;
  unsigned char*               data;
} BardMMAllocationPage;

BardMMAllocationPage* BardMMAllocationPage_create( int bytes, BardMMAllocationPage* next_page )
{
  BardMMAllocationPage* page = (BardMMAllocationPage*) BARD_XC_ALLOCATE( sizeof(BardMMAllocationPage) );

  page->data = (unsigned char*) BARD_XC_ALLOCATE( bytes );
  memset( page->data, 0, bytes );

  page->next_page = next_page;

  return page;
}

void BardMMAllocationPage_destroy( BardMMAllocationPage* page )
{
  if ( !page ) return;

  BardMMAllocationPage_destroy( page->next_page );
  BARD_XC_FREE( page );
}


//=============================================================================
// BardXCAllocator
//=============================================================================
typedef struct BardXCAllocator
{
  BardMMAllocationPage* permanent_allocation_pages;
  unsigned char*        permanent_allocation_cursor;
  int                   permanent_allocation_bytes_remaining;
  int                   initialized;

  BardMMAllocation*     small_object_pools[ BARD_XC_ALLOCATOR_SLOT_COUNT ];
    // Size  unlimited,       // [0]
    //       16, 32, 48, 64,  // [1..4]
    //       x,  x,  x,128,   // [5..8]
    //       x,  x,  x,192,   // [9..12]
    //       x,  x,  x,256    // [13..16]

} BardXCAllocator;

// A globally accessible allocator that is used by all BardMM managers to
// recycle small object allocations.
static BardXCAllocator BardXCAllocator_singleton;  // static storage initialized to 0/NULL

void  BardXCAllocator_initialize();
void  BardXCAllocator_reset();

void BardXCAllocator_initialize()
{
  if (BardXCAllocator_singleton.initialized) return;

  BardXCAllocator_reset();

  memset( &BardXCAllocator_singleton, 0, sizeof(BardXCAllocator) );

  BardXCAllocator_singleton.permanent_allocation_pages  = BardMMAllocationPage_create( BARD_XC_ALLOCATOR_PAGE_SIZE, NULL );
  BardXCAllocator_singleton.permanent_allocation_cursor = BardXCAllocator_singleton.permanent_allocation_pages->data;
  BardXCAllocator_singleton.permanent_allocation_bytes_remaining = BARD_XC_ALLOCATOR_PAGE_SIZE;

  BardXCAllocator_singleton.initialized = 1;
}

void BardXCAllocator_reset()
{
  if ( !BardXCAllocator_singleton.initialized ) return;
  BardXCAllocator_singleton.initialized = 0;

  BardMMAllocationPage_destroy( BardXCAllocator_singleton.permanent_allocation_pages );
  BardXCAllocator_singleton.permanent_allocation_pages = NULL;
}

//int total_allocations = 0;
void* BardXCAllocator_allocate_permanent( int bytes )
{
  // Returns a block of memory that will only be freed when the program terminates.
  // Benefits: very fast allocations from larger pages of memory.

  // align to 8-byte boundary
  bytes = (bytes + 7) & ~7;
  if ( !bytes ) bytes = 8;

  if (bytes > BardXCAllocator_singleton.permanent_allocation_bytes_remaining)
  {
    if (bytes > BARD_XC_ALLOCATOR_PAGE_SIZE || BardXCAllocator_singleton.permanent_allocation_bytes_remaining > 1024)
    {
      // Either it's a big allocation or it's manageable but we would be wasting too much
      // memory to start a new page now.  Make a special-sized page but don't reset the 
      // allocation cursor.
      BardXCAllocator_singleton.permanent_allocation_pages = BardMMAllocationPage_create( bytes, BardXCAllocator_singleton.permanent_allocation_pages );
      return BardXCAllocator_singleton.permanent_allocation_pages->data;
    }
    else
    {
      // Ran out of room; start a new allocation page.

      // First put all remaining space into small object caches to avoid wasting memory
      while (BardXCAllocator_singleton.permanent_allocation_bytes_remaining >= 128)
      {
        BardXCAllocator_free( BardXCAllocator_allocate_permanent(128), 128 );
      }
      while (BardXCAllocator_singleton.permanent_allocation_bytes_remaining >= 64)
      {
        BardXCAllocator_free( BardXCAllocator_allocate_permanent(64), 64 );
      }
      while (BardXCAllocator_singleton.permanent_allocation_bytes_remaining >= 48)
      {
        BardXCAllocator_free( BardXCAllocator_allocate_permanent(48), 48 );
      }
      while (BardXCAllocator_singleton.permanent_allocation_bytes_remaining >= 32)
      {
        BardXCAllocator_free( BardXCAllocator_allocate_permanent(32), 32 );
      }


      BardXCAllocator_singleton.permanent_allocation_pages = BardMMAllocationPage_create( 
          BARD_XC_ALLOCATOR_PAGE_SIZE, BardXCAllocator_singleton.permanent_allocation_pages );
      BardXCAllocator_singleton.permanent_allocation_cursor = BardXCAllocator_singleton.permanent_allocation_pages->data;
      BardXCAllocator_singleton.permanent_allocation_bytes_remaining = BARD_XC_ALLOCATOR_PAGE_SIZE;
    }
  }

  BardMMAllocation* result = (BardMMAllocation*) BardXCAllocator_singleton.permanent_allocation_cursor;
  BardXCAllocator_singleton.permanent_allocation_cursor += bytes;
  BardXCAllocator_singleton.permanent_allocation_bytes_remaining -= bytes;

  // Note: permanent allocation pages are zero-filled when first created.
  return result;
}

void* BardXCAllocator_allocate( int bytes )
{
  // INTERNAL USE
  // 
  // Contract
  // - The calling function is responsible for tracking the returned memory 
  //   and returning it via BardXCAllocator_free( void*, int )
  // - The returned memory is initialized to zero.
  if (bytes <= BARD_XC_ALLOCATOR_SMALL_OBJECT_SIZE_LIMIT)
  {
    bytes = BardXCAllocator_byte_size_to_slot_size[ bytes ];
    int pool_index = bytes >> BARD_XC_ALLOCATOR_SMALL_OBJECT_BITS;

    BardMMAllocation* result = BardXCAllocator_singleton.small_object_pools[ pool_index ];
    if (result)
    {
      BardXCAllocator_singleton.small_object_pools[pool_index] = result->next_allocation;
      memset( result, 0, bytes );
      return result;
    }
    else
    {
      return BardXCAllocator_allocate_permanent( bytes );
    }
  }
  else
  {
    void* result = BARD_XC_ALLOCATE( bytes );
    memset( result, 0, bytes );
    return result;
  }
}

void BardXCAllocator_free( void* ptr, int bytes )
{
#ifdef BARD_XC_DEBUG
  memset( ptr, -1, bytes );
#endif
  if (bytes <= BARD_XC_ALLOCATOR_SMALL_OBJECT_SIZE_LIMIT)
  {
    bytes = BardXCAllocator_byte_size_to_slot_size[ bytes ];
    int pool_index = bytes >> BARD_XC_ALLOCATOR_SMALL_OBJECT_BITS;

    BardMMAllocation* obj = (BardMMAllocation*) ptr;
    obj->next_allocation = BardXCAllocator_singleton.small_object_pools[pool_index];
    BardXCAllocator_singleton.small_object_pools[pool_index] = obj;
  }
  else
  {
    BARD_XC_FREE( (char*) ptr );
  }
}

