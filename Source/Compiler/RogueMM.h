#pragma once

//=============================================================================
//  RogueMM.h
//
//  Rogue Memory Manager header file for cross-compiled C++ code.
//
//  ---------------------------------------------------------------------------
//
//  Created 2015.01.15 by Abe Pralle
//
//  This is free and unencumbered software released into the public domain
//  under the terms of the UNLICENSE ( http://unlicense.org ).
//=============================================================================

#include "RogueTypes.h"

#ifndef ROGUEMM_PAGE_SIZE
// 4k; should be a multiple of 256 if redefined
#  define ROGUEMM_PAGE_SIZE (4*1024)
#endif

enum
{
  ROGUEMM_SLOT_COUNT               =  5,
  // - The slot index times 64 is the allocation size of that slot; slot
  //   0 is used for native allocations of all sizes and slots 1..4 are block 
  //   sizes 64, //   128, 192, and 256.

  ROGUEMM_GRANULARITY_BITS         =  6,
  ROGUEMM_GRANULARITY_SIZE         =  (1 << ROGUEMM_GRANULARITY_BITS),
  // 64 byte step size in allocations

  ROGUEMM_SMALL_ALLOCATION_SIZE_LIMIT  = ((ROGUEMM_SLOT_COUNT-1) << ROGUEMM_GRANULARITY_BITS)
  // Small allocation limit is 256 bytes - afterwards objects are allocated
  // from the system.
};

struct RogueMMAllocationPage
{
  // Backs small 0..256-byte allocations.
  RogueMMAllocationPage* next_page;

  RogueByte data[ ROGUEMM_PAGE_SIZE ];

  RogueMMAllocationPage( RogueMMAllocationPage* next_page );
};


//-----------------------------------------------------------------------------
//  RogueMMAllocator
//-----------------------------------------------------------------------------
struct RogueMMAllocator
{
  virtual ~RogueMMAllocator() {}

  virtual RogueObject* allocate( int size ) = 0;
  virtual RogueObject* free( RogueObject* obj ) = 0;
};

//-----------------------------------------------------------------------------
//  RogueMMBlockAllocator
//-----------------------------------------------------------------------------
struct RogueMMBlockAllocator : RogueMMAllocator
{
  RogueMMAllocationPage* pages;
  RogueObject* free_objects;
  int block_size;   // 64, 128, 192, 256

  RogueMMBlockAllocator( int block_size );
  ~RogueMMBlockAllocator();

  RogueObject* allocate( int size );
  RogueObject* free( RogueObject* obj );
};

//-----------------------------------------------------------------------------
//  RogueMMSystemAllocator
//-----------------------------------------------------------------------------
struct RogueMMSystemAllocator : RogueMMAllocator
{
  RogueObject* allocate( int size );
  RogueObject* free( RogueObject* obj );
};


//-----------------------------------------------------------------------------
//  RogueMM
//-----------------------------------------------------------------------------
struct RogueMM
{
  RogueMMAllocator* allocators[ROGUEMM_SLOT_COUNT];
  // [1] = allocator for block size 64, [4] = block size 256

  RogueMM();
  ~RogueMM();
};

/*



struct RogueMM;
struct RogueMMType;
struct RogueMMInfo;

typedef struct RogueMM     RogueMM;
typedef struct RogueMMType RogueMMType;

#if defined(_WIN32)
  typedef double           RogueMMReal64;
  typedef float            RogueMMReal32;
  typedef __int64          RogueMMInteger64;
  typedef __int32          RogueMMInteger32;
  typedef unsigned __int16 RogueMMCharacter;
  typedef unsigned char    RogueMMByte;
  typedef RogueMMInteger32    RogueMMLogical;
#else
  typedef double           RogueMMReal64;
  typedef float            RogueMMReal32;
  typedef int64_t          RogueMMInteger64;
  typedef int32_t          RogueMMInteger32;
  typedef uint16_t         RogueMMCharacter;
  typedef uint8_t          RogueMMByte;
  typedef RogueMMInteger32    RogueMMLogical;
#endif

typedef void (*RogueMMCallback)(void*);

#define RogueMMObject_reference(ptr) \
  { \
    RogueMMInfo* _RogueMM_ref_obj = (RogueMMInfo*) ptr; \
    if (_RogueMM_ref_obj && _RogueMM_ref_obj->size > 0) \
    {  \
      _RogueMM_ref_obj->size = ~_RogueMM_ref_obj->size; \
      _RogueMM_ref_obj->next_reference = _RogueMM_ref_obj->type->crom->referenced_objects; \
      _RogueMM_ref_obj->type->crom->referenced_objects = _RogueMM_ref_obj;  \
    } \
  }

#define RogueMM_calculate_offset( Type, ref_name ) \
  (int)((intptr_t) &(((Type*)((void*)0))->ref_name))

#define RogueMMType_add_reference_offset( type, offset ) \
  (type)->reference_offsets[ (type)->reference_offset_count++ ] = offset

//=============================================================================
// RogueMMAllocationPage
//=============================================================================
typedef struct RogueMMAllocationPage
{
  struct RogueMMAllocationPage* next_page;
  unsigned char*             data;
} RogueMMAllocationPage;


//=============================================================================
//  RogueMM
//=============================================================================
struct RogueMM
{
  RogueMMAllocationPage* pages;
  unsigned char*      allocation_cursor;
  int                 allocation_bytes_remaining;

  //---------------------------------------------------------------------------
  // Lists linked via .next_object
  //---------------------------------------------------------------------------
  struct RogueMMInfo*  disposable_objects;
  // Linked list of objects that can be freed without cleanup when they're
  // no longer referenced.  Linked by .next_object.  It is common for most 
  // objects to be 'disposable'.  Linked by .next_object.

  struct RogueMMInfo*  objects_requiring_cleanup;
  // Linked list of objects that can must be cleaned up when they're no longer
  // referenced.  Objects that must close files or free native pointers should
  // be of this variety.
  //
  // Objects are placed on this list when their type includes an on_destroy
  // callback.  When the object is unreferenced after a trace, the callback
  // is called and the object is moved to the disposable_objects list.
  //
  // Linked by .next_object.

  //---------------------------------------------------------------------------
  // List linked via .next_reference - built by tracing and collecting objects
  // from 'disposable_objects' and 'objects_requiring_cleanup'.
  //---------------------------------------------------------------------------
  struct RogueMMInfo*  referenced_objects;
  // Linked list that acts as an object queue during GC trace

  //---------------------------------------------------------------------------

  struct RogueMMInfo*  small_object_pools[ ROGUEMM_SLOT_COUNT ];
  // 64, 64, 128, 192, 256 [slots 0..4]
};

RogueMM* RogueMM_create();
RogueMM* RogueMM_destroy( RogueMM* crom );

RogueMM* RogueMM_init( RogueMM* crom );
RogueMM* RogueMM_retire( RogueMM* crom );

void  RogueMM_print_state( RogueMM* crom );

// Memory allocation
void* RogueMM_allocate( RogueMM* crom, int size );
void  RogueMM_free( RogueMM* crom, void* ptr, int size );
void* RogueMM_reserve( RogueMM* crom, int size );

void RogueMM_trace_referenced_objects( RogueMM* crom );
void RogueMM_free_unreferenced_objects( RogueMM* crom );
void RogueMM_collect_garbage( RogueMM* crom );

//=============================================================================
//  RogueMMType
//=============================================================================
struct RogueMMType
{
  RogueMM*     crom;

  void* info;  // arbitrary info pointer, can be set or left unused as desired.

  RogueMMCallback  on_trace;
  RogueMMCallback  on_destroy;

  int* reference_offsets;
  int  reference_offset_count;
};

RogueMMType* RogueMMType_init( RogueMMType* type_or_null, RogueMM* crom );

// Object and Array creation
void*     RogueMMType_create_object( RogueMMType* of_type, int object_size );


//=============================================================================
//  RogueMMInfo
//=============================================================================
typedef struct RogueMMInfo
{
  struct RogueMMInfo* next_object;
  // Pointer to next allocation in linked list; primary means of tracking
  // allocations as well as free objects. 

  struct RogueMMInfo* next_reference;
  // Used during GC to build a list of referenced objects that can be iterated
  // over later.

  RogueMMType* type;
  // The runtime type of this object.

  int size;
  // Byte size of object.  Used when freeing objects as well as during GC
  // to identify traced objects with a negation.  Not part of 'type' since
  // RogueMM objects of the same type can have different sizes, such as arrays.

} RogueMMInfo;



//=============================================================================
//  DEFINITIONS
//=============================================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//-----------------------------------------------------------------------------
//  RogueMMAllocationPage_create
//-----------------------------------------------------------------------------
RogueMMAllocationPage* RogueMMAllocationPage_create( int size, RogueMMAllocationPage* next_page )
{
  RogueMMAllocationPage* page = (RogueMMAllocationPage*) ROGUEMM_SYSTEM_MALLOC( sizeof(RogueMMAllocationPage) );

  page->data = (unsigned char*) ROGUEMM_SYSTEM_MALLOC( size );

#if !defined(ROGUEMM_DISABLE_MEMCLEAR)
  memset( page->data, 0, size );
#endif

  page->next_page = next_page;

  return page;
}

//=============================================================================
// RogueMM
//=============================================================================

//-----------------------------------------------------------------------------
//  RogueMM_create()
//-----------------------------------------------------------------------------
RogueMM* RogueMM_create()
{
  return RogueMM_init( (RogueMM*) ROGUEMM_SYSTEM_MALLOC(sizeof(RogueMM)) );
}

//-----------------------------------------------------------------------------
//  RogueMM_destroy()
//-----------------------------------------------------------------------------
RogueMM* RogueMM_destroy( RogueMM* crom )
{
  if (crom)
  {
    RogueMM_retire( crom );
    ROGUEMM_SYSTEM_FREE( crom );
  }
  return 0;
}

RogueMM* RogueMM_init( RogueMM* crom )
{
  memset( crom, 0, sizeof(RogueMM) );
  return crom;
}

RogueMM* RogueMM_retire( RogueMM* crom )
{
  if (crom)
  {
    RogueMMAllocationPage* cur_page = crom->pages;
    while (cur_page)
    {
      RogueMMAllocationPage* next_page = cur_page->next_page;
      ROGUEMM_SYSTEM_FREE( cur_page );
      cur_page = next_page;
    }
    memset( crom, 0, sizeof(RogueMM) );
  }

  return crom;
}

static int RogueMM_count_next_object( RogueMMInfo* obj )
{
  int n = 0;
  while (obj)
  {
    ++n;
    obj = obj->next_object;
  }
  return n;
}

void  RogueMM_print_state( RogueMM* crom )
{
  printf( "-------------------------------------------------------------------------------\n" );
  printf( "ROGUEMM STATUS\n" );

  // Allocation pages
  {
    int n = 0;
    for (RogueMMAllocationPage* cur=crom->pages; cur; cur=cur->next_page) ++n;
    printf( "Allocation pages: %d\n", n );
  }

  printf( "Free small object allocations [64, 128, 192, 256]: " );
  printf( "%d, ", RogueMM_count_next_object(crom->small_object_pools[1]) );
  printf( "%d, ", RogueMM_count_next_object(crom->small_object_pools[2]) );
  printf( "%d, ", RogueMM_count_next_object(crom->small_object_pools[3]) );
  printf( "%d\n", RogueMM_count_next_object(crom->small_object_pools[4]) );

  printf( "Active disposable objects: %d\n", RogueMM_count_next_object(crom->disposable_objects) );
  printf( "Active objects requiring cleanup: %d\n", RogueMM_count_next_object(crom->objects_requiring_cleanup) );

  printf( "-------------------------------------------------------------------------------\n" );
}

void* RogueMM_reserve( RogueMM* crom, int size )
{
  // Returns a block of memory that will only be freed when the program terminates.
  // Benefits: Very fast and efficient allocations from larger pages of memory.
  // Caveat: Reserved allocations are not returned to the system until the RogueMM object is destroyed.

  // Align to 8-byte boundary
  size = (size + 7) & ~7;
  if ( !size ) size = 8;

  if (size > crom->allocation_bytes_remaining)
  {
    // We don't have enough room on the current page.
    if (size > ROGUEMM_PAGE_SIZE)
    {
      // Make a special-sized page but don't reset the allocation cursor.
      crom->pages = RogueMMAllocationPage_create( size, crom->pages );
      return crom->pages->data;
    }
    else
    {
      // Ran out of room; start a new allocation page.

      // First put all remaining space into small object caches to avoid wasting memory
      int allocation_size;
      for (allocation_size=ROGUEMM_SMALL_ALLOCATION_SIZE_LIMIT; 
           allocation_size>=ROGUEMM_GRANULARITY_SIZE; 
           allocation_size-=ROGUEMM_GRANULARITY_SIZE)
      {
        // Loop from largest smallest allocation size.
        while (crom->allocation_bytes_remaining >= allocation_size)
        {
          // Must use RogueMM_reserve() rather than RogueMM_allocate() to prevent
          // repeatedly getting the same object from the pool.
          RogueMM_free( crom, RogueMM_reserve(crom,allocation_size), allocation_size );
        }
      }

      crom->pages = RogueMMAllocationPage_create( ROGUEMM_PAGE_SIZE, crom->pages );
      crom->allocation_cursor = crom->pages->data;
      crom->allocation_bytes_remaining = ROGUEMM_PAGE_SIZE;
    }
  }

  {
    RogueMMInfo* result = (RogueMMInfo*) crom->allocation_cursor;
    crom->allocation_cursor += size;
    crom->allocation_bytes_remaining -= size;

    // Note: reserved allocation pages are zero-filled when first created.
    return result;
  }
}

void* RogueMM_allocate( RogueMM* crom, int size )
{
  // INTERNAL USE
  // 
  // Contract
  // - The calling function is responsible for tracking the returned memory 
  //   and returning it via RogueMM_free( void*, int )
  // - The returned memory is initialized to zero.
  RogueMMInfo* result;
  if (size <= 0) size = ROGUEMM_GRANULARITY_SIZE;
  if (size <= ROGUEMM_SMALL_ALLOCATION_SIZE_LIMIT)
  {
    int pool_index = (size >> ROGUEMM_GRANULARITY_BITS) + ((size & (ROGUEMM_GRANULARITY_SIZE-1)) != 0);
    size = (pool_index << ROGUEMM_GRANULARITY_BITS); // expand size to full capacity

    result = crom->small_object_pools[ pool_index ];
    if (result)
    {
      crom->small_object_pools[pool_index] = result->next_object;
#ifdef ROGUEMM_DISABLE_MEMCLEAR
      memset( result, 0, sizeof(RogueMMInfo) );  // only zero the header
#else
      memset( result, 0, size );  // zero everything
#endif
    }
    else
    {
      result = (RogueMMInfo*) RogueMM_reserve( crom, size );
    }
  }
  else
  {
    result = (RogueMMInfo*) ROGUEMM_SYSTEM_MALLOC( size );
#ifdef ROGUEMM_DISABLE_MEMCLEAR
      memset( result, 0, sizeof(RogueMMInfo) );  // only zero the header
#else
      memset( result, 0, size );  // zero everything
#endif
  }
  result->size = size;
  return result;
}

void RogueMM_free( RogueMM* crom, void* ptr, int size )
{
#ifdef ROGUEMM_DEBUG
  memset( ptr, -1, size );
#endif
  if (size <= ROGUEMM_SMALL_ALLOCATION_SIZE_LIMIT)
  {
    RogueMMInfo* obj;
    int pool_index = (size >> ROGUEMM_GRANULARITY_BITS);

    obj = (RogueMMInfo*) ptr;
    obj->next_object = crom->small_object_pools[pool_index];
    crom->small_object_pools[pool_index] = obj;
  }
  else
  {
    ROGUEMM_SYSTEM_FREE( (char*) ptr );
  }
}

void RogueMM_trace_referenced_objects( RogueMM* crom )
{
  // External root objects and retained objects should already have be added to the 
  // referenced_objects queue via RogueMMObject_reference().

  // Trace through each object on the referenced_objects queue until queue is empty.
  while (crom->referenced_objects)
  {
    // Remove the next object off the pending list and trace it.
    RogueMMInfo* cur = crom->referenced_objects;
    RogueMMType* type = cur->type;

    crom->referenced_objects = cur->next_reference;

    if (type->on_trace)
    {
      // Invoke custom handler for object arrays and other special objects
      type->on_trace( cur );
    }
    else
    {
      // Use default - trace internal references using the reference_offsets list.
      int  count = type->reference_offset_count;
      int* cur_offset = type->reference_offsets - 1;
      while (--count >= 0)
      {
        RogueMMObject_reference( (*((RogueMMInfo**)(((RogueMMByte*)cur) + *(++cur_offset)))) );
      }
    }
  }
}

void RogueMM_free_unreferenced_objects( RogueMM* crom )
{
  // Disposable objects - reset referenced and free unreferenced.
  {
    RogueMMInfo* surviving_objects = NULL;
    RogueMMInfo* cur = crom->disposable_objects;
    while (cur)
    {
      RogueMMInfo* next = cur->next_object;
      if (cur->size < 0)
      {
        // Referenced; flip indicator and link into surviving objects list.
        cur->size = ~cur->size;
        cur->next_object = surviving_objects;
        surviving_objects = cur;
      }
      else
      {
        // Unreferenced; free allocation.
        RogueMM_free( crom, cur, cur->size );
      }
      cur = next;
    }
    crom->disposable_objects = surviving_objects;
  }

  // Call on_destroy on any unreferenced objects requiring cleanup
  // and move them to the regular object list.  This must be done 
  // after unreferenced objects are freed because on_destroy() could 
  // theoretically introduce all objects in an an unreferenced 
  // island back into the main set.
  {
    RogueMMInfo* cur = crom->objects_requiring_cleanup;
    crom->objects_requiring_cleanup = 0;
    while (cur)
    {
      RogueMMInfo* next = cur->next_object;
      if (cur->size < 0)
      {
        // Referenced; leave on objects_requiring_cleanup list.
        cur->size = ~cur->size;
        cur->next_object = crom->objects_requiring_cleanup;
        crom->objects_requiring_cleanup = cur;
      }
      else
      {
        // Unreferenced; move to regular object list and call on_destroy().
        // Note that on_destroy() could create additional new objects
        // which is why we can't catch crom->objects_requiring_cleanup[] and
        // crom->disposable_objects[] as local lists.
        cur->next_object = crom->disposable_objects;
        crom->disposable_objects = cur;

        cur->type->on_destroy( cur );
      }
      cur = next;
    }
  }
}

void RogueMM_collect_garbage( RogueMM* crom )
{
  RogueMM_trace_referenced_objects( crom );
  RogueMM_free_unreferenced_objects( crom );
}


//=============================================================================
//  RogueMMType
//=============================================================================
RogueMMType* RogueMMType_init( RogueMMType* type, struct RogueMM* crom )
{
  if ( !type ) type = (RogueMMType*) RogueMM_reserve( crom, sizeof(RogueMMType) );
  memset( type, 0, sizeof(RogueMMType) );

  type->crom = crom;

  return type;
}

void* RogueMMType_create_object( RogueMMType* of_type, int object_size )
{
  RogueMMInfo* obj;
  RogueMM* crom = of_type->crom;

  obj = (RogueMMInfo*) RogueMM_allocate( crom, object_size );
  obj->type = of_type;

  if (of_type->on_destroy)
  {
    obj->next_object = crom->objects_requiring_cleanup;
    crom->objects_requiring_cleanup = obj;
  }
  else
  {
    obj->next_object = crom->disposable_objects;
    crom->disposable_objects = obj;
  }

  return obj;
}
*/
