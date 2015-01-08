#ifndef CROM_H
#define CROM_H

#ifndef CROM_SYSTEM_MALLOC
#  define CROM_SYSTEM_MALLOC(size) malloc(size)
#endif

#ifndef CROM_SYSTEM_FREE
#  define CROM_SYSTEM_FREE(ptr) free(ptr)
#endif

void* Crom_malloc( int size );

#define Crom_allocate(crom,size) Crom_malloc(size)
#define Crom_free(crom,ptr,size) CROM_SYSTEM_FREE(ptr)

#ifdef NOCOMPILE
//=============================================================================
//  Crom.h
//
//  ---------------------------------------------------------------------------
//
//  USAGE OVERVIEW
//
//  1.  Create a Crom object manager with Crom_create().
//
//  2.  Allocate and free manually-tracked allocations with Crom_allocate()
//      and Crom_free(), and/or create garbage-collected objects as
//      described in the following steps.
//
//  3.  Define object types (classes) with Crom_define_object_type().
//
//  4.  Create objects as desired with CromType_create_object() and 
//      Object data is automatically initialized to zero.
//
//  6.  Just before you wish to invoke a GC, call CromObject_reference() on
//      any known external global references that are unknown to Crom - root 
//      objects and objects on the stack, etc.
//
//  7.  Call Crom_collect_garbage() to trace all objects, starting with known
//      global references as defined in Step 6.  All unreferenced (untraced)
//      objects will be freed.
//
//  8.  Call Crom_destroy() to free all objects and other memory used by
//      Crom.
//
//
//  ADDITONAL NOTES
//
//  2.  You can use the memory allocated by Crom_allocate() for any purpose,
//      such as creating array data, object data, or linked lists nodes not 
//      directly related to Crom.  Crom cannot automatically GC that data
//      (in general you must manually free it with Crom_free()) but you will 
//      still benefit from the speed and efficiency of small object caching.
//
//      All memory obtained via Crom_allocate() will be automatically freed
//      when Crom_destroy() is called. 
//
//  3.  By default Crom automatically follows each object's references while 
//      tracing.  The locations of the references within the 
//      application-specific objects are defined in the CromType associated
//      with each object.  Pass the total number of references in 
//      Crom_define_object_type() and then define the offset to each reference
//      with CromType_add_reference_offset().
//
//  4.  An 'on_trace' callback can be sent to Crom_define_object_type().  
//      Objects with this callback will call it with each object being traced
//      instead of performing the default trace.  The callback should call
//      CromObject_reference() on any objects it references.
//
//  5.  An 'on_destroy' callback can be sent to Crom_define_object_type().
//      Objects with this callback will call it when they are no longer
//      referenced.
//
//  ---------------------------------------------------------------------------
//
//  HISTORY
//
//  Created 2014.06.15 by Abe Pralle
//
//  This is free and unencumbered software released into the public domain
//  under the terms of the UNLICENSE ( http://unlicense.org ).
//
//=============================================================================

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
//  DEFINES AND MACROS
//
//  Optional compiler defines.  You can either #define before #including
//  Crom.h or you can define as a compiler option in a compiler-dependent way
//  (command line: -D"CROM_SYSTEM_MALLOC(size)=some_alloc_fn(size)" ).
//
//  CROM_SYSTEM_MALLOC(size)
//    Use something besides malloc().
//
//  CROM_SYSTEM_FREE(ptr)
//    Use something besides free().
//
//  CROM_PAGE_SIZE
//    The total size of an individual page that will be used for small, 
//    recycled allocations.  The default is 4k.  In the worst case, about 
//    that much memory will be wasted if the "high water mark" of small memory
//    allocations ends up using only a small piece of the last page allocated.
//    On the other hand smaller page sizes use more system time and memory to
//    allocate.
//     
//  CROM_DISABLE_MEMCLEAR
//    Define to prevent Crom from clearing allocated array and object data.
//
//  CROM_DEBUG
//    Define to enable built-in debugging options. 
//=============================================================================

#ifndef CROM_PAGE_SIZE
#  define CROM_PAGE_SIZE (4*1024)
#endif

enum
{
  CROM_SLOT_COUNT               =  5,
  // - The slot index times 64 is the allocation size of that slot.
  // - Not all slots are used - allocator restricts values to maximize block
  //   reuse for varying sizes.  Only sizes 64, 128, 192, and 256 are used.

  CROM_GRANULARITY_BITS         =  6,
  CROM_GRANULARITY_SIZE         =  (1 << CROM_GRANULARITY_BITS),
  // 64 byte step size in allocations

  CROM_SMALL_OBJECT_SIZE_LIMIT  = ((CROM_SLOT_COUNT-1) << CROM_GRANULARITY_BITS)
  // Small allocation limit is 256 bytes - afterwards objects are allocated
  // from the system.
};


struct Crom;
struct CromType;
struct CromInfo;

typedef struct Crom     Crom;
typedef struct CromType CromType;

#if defined(_WIN32)
  typedef double           CromReal64;
  typedef float            CromReal32;
  typedef __int64          CromInteger64;
  typedef __int32          CromInteger32;
  typedef unsigned __int16 CromCharacter;
  typedef unsigned char    CromByte;
  typedef CromInteger32    CromLogical;
#else
  typedef double           CromReal64;
  typedef float            CromReal32;
  typedef int64_t          CromInteger64;
  typedef int32_t          CromInteger32;
  typedef uint16_t         CromCharacter;
  typedef uint8_t          CromByte;
  typedef CromInteger32    CromLogical;
#endif

typedef void (*CromCallback)(void*);

#define CromObject_reference(ptr) \
  { \
    CromInfo* _Crom_ref_obj = (CromInfo*) ptr; \
    if (_Crom_ref_obj && _Crom_ref_obj->size > 0) \
    {  \
      _Crom_ref_obj->size = ~_Crom_ref_obj->size; \
      _Crom_ref_obj->next_reference = _Crom_ref_obj->type->crom->referenced_objects; \
      _Crom_ref_obj->type->crom->referenced_objects = _Crom_ref_obj;  \
    } \
  }

#define Crom_calculate_offset( Type, ref_name ) \
  (int)((intptr_t) &(((Type*)((void*)0))->ref_name))

#define CromType_add_reference_offset( type, offset ) \
  (type)->reference_offsets[ (type)->reference_offset_count++ ] = offset

//=============================================================================
// CromAllocationPage
//=============================================================================
typedef struct CromAllocationPage
{
  struct CromAllocationPage* next_page;
  unsigned char*             data;
} CromAllocationPage;


//=============================================================================
//  Crom
//=============================================================================
struct Crom
{
  CromAllocationPage* pages;
  unsigned char*      allocation_cursor;
  int                 allocation_bytes_remaining;

  //---------------------------------------------------------------------------
  // Lists linked via .next_object
  //---------------------------------------------------------------------------
  struct CromInfo*  disposable_objects;
  // Linked list of objects that can be freed without cleanup when they're
  // no longer referenced.  Linked by .next_object.  It is common for most 
  // objects to be 'disposable'.  Linked by .next_object.

  struct CromInfo*  objects_requiring_cleanup;
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
  struct CromInfo*  referenced_objects;
  // Linked list that acts as an object queue during GC trace

  //---------------------------------------------------------------------------

  struct CromInfo*  small_object_pools[ CROM_SLOT_COUNT ];
  // 64, 64, 128, 192, 256 [slots 0..4]
};

Crom* Crom_create();
Crom* Crom_destroy( Crom* crom );

Crom* Crom_init( Crom* crom );
Crom* Crom_retire( Crom* crom );

void  Crom_print_state( Crom* crom );

// Memory allocation
void* Crom_allocate( Crom* crom, int size );
void  Crom_free( Crom* crom, void* ptr, int size );
void* Crom_reserve( Crom* crom, int size );

void Crom_trace_referenced_objects( Crom* crom );
void Crom_free_unreferenced_objects( Crom* crom );
void Crom_collect_garbage( Crom* crom );

//=============================================================================
//  CromType
//=============================================================================
struct CromType
{
  Crom*     crom;

  void* info;  // arbitrary info pointer, can be set or left unused as desired.

  CromCallback  on_trace;
  CromCallback  on_destroy;

  int* reference_offsets;
  int  reference_offset_count;
};

CromType* CromType_init( CromType* type_or_null, Crom* crom );

// Object and Array creation
void*     CromType_create_object( CromType* of_type, int object_size );


//=============================================================================
//  CromInfo
//=============================================================================
typedef struct CromInfo
{
  struct CromInfo* next_object;
  // Pointer to next allocation in linked list; primary means of tracking
  // allocations as well as free objects. 

  struct CromInfo* next_reference;
  // Used during GC to build a list of referenced objects that can be iterated
  // over later.

  CromType* type;
  // The runtime type of this object.

  int size;
  // Byte size of object.  Used when freeing objects as well as during GC
  // to identify traced objects with a negation.  Not part of 'type' since
  // Crom objects of the same type can have different sizes, such as arrays.

} CromInfo;


#ifdef __cplusplus
}
#endif

//=============================================================================
//  DEFINITIONS
//=============================================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//-----------------------------------------------------------------------------
//  CromAllocationPage_create
//-----------------------------------------------------------------------------
CromAllocationPage* CromAllocationPage_create( int size, CromAllocationPage* next_page )
{
  CromAllocationPage* page = (CromAllocationPage*) CROM_SYSTEM_MALLOC( sizeof(CromAllocationPage) );

  page->data = (unsigned char*) CROM_SYSTEM_MALLOC( size );

#if !defined(CROM_DISABLE_MEMCLEAR)
  memset( page->data, 0, size );
#endif

  page->next_page = next_page;

  return page;
}

//=============================================================================
// Crom
//=============================================================================

//-----------------------------------------------------------------------------
//  Crom_create()
//-----------------------------------------------------------------------------
Crom* Crom_create()
{
  return Crom_init( (Crom*) CROM_SYSTEM_MALLOC(sizeof(Crom)) );
}

//-----------------------------------------------------------------------------
//  Crom_destroy()
//-----------------------------------------------------------------------------
Crom* Crom_destroy( Crom* crom )
{
  if (crom)
  {
    Crom_retire( crom );
    CROM_SYSTEM_FREE( crom );
  }
  return 0;
}

Crom* Crom_init( Crom* crom )
{
  memset( crom, 0, sizeof(Crom) );
  return crom;
}

Crom* Crom_retire( Crom* crom )
{
  if (crom)
  {
    CromAllocationPage* cur_page = crom->pages;
    while (cur_page)
    {
      CromAllocationPage* next_page = cur_page->next_page;
      CROM_SYSTEM_FREE( cur_page );
      cur_page = next_page;
    }
    memset( crom, 0, sizeof(Crom) );
  }

  return crom;
}

static int Crom_count_next_object( CromInfo* obj )
{
  int n = 0;
  while (obj)
  {
    ++n;
    obj = obj->next_object;
  }
  return n;
}

void  Crom_print_state( Crom* crom )
{
  printf( "-------------------------------------------------------------------------------\n" );
  printf( "CROM STATUS\n" );

  // Allocation pages
  {
    int n = 0;
    for (CromAllocationPage* cur=crom->pages; cur; cur=cur->next_page) ++n;
    printf( "Allocation pages: %d\n", n );
  }

  printf( "Free small object allocations [64, 128, 192, 256]: " );
  printf( "%d, ", Crom_count_next_object(crom->small_object_pools[1]) );
  printf( "%d, ", Crom_count_next_object(crom->small_object_pools[2]) );
  printf( "%d, ", Crom_count_next_object(crom->small_object_pools[3]) );
  printf( "%d\n", Crom_count_next_object(crom->small_object_pools[4]) );

  printf( "Active disposable objects: %d\n", Crom_count_next_object(crom->disposable_objects) );
  printf( "Active objects requiring cleanup: %d\n", Crom_count_next_object(crom->objects_requiring_cleanup) );

  printf( "-------------------------------------------------------------------------------\n" );
}

void* Crom_reserve( Crom* crom, int size )
{
  // Returns a block of memory that will only be freed when the program terminates.
  // Benefits: Very fast and efficient allocations from larger pages of memory.
  // Caveat: Reserved allocations are not returned to the system until the Crom object is destroyed.

  // Align to 8-byte boundary
  size = (size + 7) & ~7;
  if ( !size ) size = 8;

  if (size > crom->allocation_bytes_remaining)
  {
    // We don't have enough room on the current page.
    if (size > CROM_PAGE_SIZE)
    {
      // Make a special-sized page but don't reset the allocation cursor.
      crom->pages = CromAllocationPage_create( size, crom->pages );
      return crom->pages->data;
    }
    else
    {
      // Ran out of room; start a new allocation page.

      // First put all remaining space into small object caches to avoid wasting memory
      int allocation_size;
      for (allocation_size=CROM_SMALL_OBJECT_SIZE_LIMIT; 
           allocation_size>=CROM_GRANULARITY_SIZE; 
           allocation_size-=CROM_GRANULARITY_SIZE)
      {
        // Loop from largest smallest allocation size.
        while (crom->allocation_bytes_remaining >= allocation_size)
        {
          // Must use Crom_reserve() rather than Crom_allocate() to prevent
          // repeatedly getting the same object from the pool.
          Crom_free( crom, Crom_reserve(crom,allocation_size), allocation_size );
        }
      }

      crom->pages = CromAllocationPage_create( CROM_PAGE_SIZE, crom->pages );
      crom->allocation_cursor = crom->pages->data;
      crom->allocation_bytes_remaining = CROM_PAGE_SIZE;
    }
  }

  {
    CromInfo* result = (CromInfo*) crom->allocation_cursor;
    crom->allocation_cursor += size;
    crom->allocation_bytes_remaining -= size;

    // Note: reserved allocation pages are zero-filled when first created.
    return result;
  }
}

void* Crom_allocate( Crom* crom, int size )
{
  // INTERNAL USE
  // 
  // Contract
  // - The calling function is responsible for tracking the returned memory 
  //   and returning it via Crom_free( void*, int )
  // - The returned memory is initialized to zero.
  CromInfo* result;
  if (size <= 0) size = CROM_GRANULARITY_SIZE;
  printf("crom alloc\n");
  if (size <= CROM_SMALL_OBJECT_SIZE_LIMIT)
  {
    int pool_index = (size >> CROM_GRANULARITY_BITS) + ((size & (CROM_GRANULARITY_SIZE-1)) != 0);
    size = (pool_index << CROM_GRANULARITY_BITS); // expand size to full capacity

    result = crom->small_object_pools[ pool_index ];
    if (result)
    {
      crom->small_object_pools[pool_index] = result->next_object;
#ifdef CROM_DISABLE_MEMCLEAR
      memset( result, 0, sizeof(CromInfo) );  // only zero the header
#else
      memset( result, 0, size );  // zero everything
#endif
    }
    else
    {
      result = (CromInfo*) Crom_reserve( crom, size );
    }
  }
  else
  {
    result = (CromInfo*) CROM_SYSTEM_MALLOC( size );
#ifdef CROM_DISABLE_MEMCLEAR
      memset( result, 0, sizeof(CromInfo) );  // only zero the header
#else
      memset( result, 0, size );  // zero everything
#endif
  }
  result->size = size;
  return result;
}

void Crom_free( Crom* crom, void* ptr, int size )
{
#ifdef CROM_DEBUG
  memset( ptr, -1, size );
#endif
  if (size <= CROM_SMALL_OBJECT_SIZE_LIMIT)
  {
    CromInfo* obj;
    int pool_index = (size >> CROM_GRANULARITY_BITS);

    obj = (CromInfo*) ptr;
    obj->next_object = crom->small_object_pools[pool_index];
    crom->small_object_pools[pool_index] = obj;
  }
  else
  {
    CROM_SYSTEM_FREE( (char*) ptr );
  }
}

void Crom_trace_referenced_objects( Crom* crom )
{
  // External root objects and retained objects should already have be added to the 
  // referenced_objects queue via CromObject_reference().

  // Trace through each object on the referenced_objects queue until queue is empty.
  while (crom->referenced_objects)
  {
    // Remove the next object off the pending list and trace it.
    CromInfo* cur = crom->referenced_objects;
    CromType* type = cur->type;

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
        CromObject_reference( (*((CromInfo**)(((CromByte*)cur) + *(++cur_offset)))) );
      }
    }
  }
}

void Crom_free_unreferenced_objects( Crom* crom )
{
  // Disposable objects - reset referenced and free unreferenced.
  {
    CromInfo* surviving_objects = NULL;
    CromInfo* cur = crom->disposable_objects;
    while (cur)
    {
      CromInfo* next = cur->next_object;
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
        Crom_free( crom, cur, cur->size );
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
    CromInfo* cur = crom->objects_requiring_cleanup;
    crom->objects_requiring_cleanup = 0;
    while (cur)
    {
      CromInfo* next = cur->next_object;
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

void Crom_collect_garbage( Crom* crom )
{
  Crom_trace_referenced_objects( crom );
  Crom_free_unreferenced_objects( crom );
}


//=============================================================================
//  CromType
//=============================================================================
CromType* CromType_init( CromType* type, struct Crom* crom )
{
  if ( !type ) type = (CromType*) Crom_reserve( crom, sizeof(CromType) );
  memset( type, 0, sizeof(CromType) );

  type->crom = crom;

  return type;
}

void* CromType_create_object( CromType* of_type, int object_size )
{
  CromInfo* obj;
  Crom* crom = of_type->crom;

  obj = (CromInfo*) Crom_allocate( crom, object_size );
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

#endif // NOCOMPILE

#endif // CROM_H
