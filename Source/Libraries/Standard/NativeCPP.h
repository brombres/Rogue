#pragma once

//=============================================================================
//  Rogue.h
//
//  ---------------------------------------------------------------------------
//
//  Created 2015.01.19 by Abe Pralle
//
//  This is free and unencumbered software released into the public domain
//  under the terms of the UNLICENSE ( http://unlicense.org ).
//=============================================================================

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <cstdint>
#endif

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  typedef double           RogueReal;
  typedef float            RogueFloat;
  typedef __int64          RogueLong;
  typedef __int32          RogueInteger;
  typedef unsigned __int16 RogueCharacter;
  typedef unsigned char    RogueByte;
  typedef bool             RogueLogical;
#else
  typedef double           RogueReal;
  typedef float            RogueFloat;
  typedef int64_t          RogueLong;
  typedef int32_t          RogueInteger;
  typedef uint16_t         RogueCharacter;
  typedef uint8_t          RogueByte;
  typedef bool             RogueLogical;
#endif

struct RogueAllocator;
struct RogueString;
struct RogueCharacterList;

#define ROGUE_CREATE_OBJECT(name) RogueType_create_object(RogueType##name,0)
  //e.g. RogueType_create_object(RogueStringBuilder,0)

#define ROGUE_SINGLETON(name) RogueType_singleton(RogueType##name)
  //e.g. RogueType_singleton( RogueTypeConsole )

#define ROGUE_PROPERTY(name) p_##name

struct RogueObject;

typedef void (*RogueTraceFn)( void* obj );
typedef RogueObject* (*RogueInitFn)( void* obj );

//-----------------------------------------------------------------------------
//  RogueType
//-----------------------------------------------------------------------------

struct RogueType
{
  int          base_type_count;
  RogueType**  base_types;

  int          index;  // used for aspect call dispatch
  int          object_size;

  RogueObject* _singleton;
  void**       methods;

  RogueAllocator*   allocator;

  RogueTraceFn      trace_fn;
  RogueInitFn       init_object_fn;
  RogueInitFn       init_fn;
};

RogueObject* RogueType_create_object( RogueType* THIS, RogueInteger size );
RogueType*   RogueType_retire( RogueType* THIS );
RogueObject* RogueType_singleton( RogueType* THIS );

//-----------------------------------------------------------------------------
//  RogueObject
//-----------------------------------------------------------------------------
struct RogueObjectType : RogueType
{
};

struct RogueObject
{
  RogueObject* next_object;
  // Used to keep track of this allocation so that it can be freed when no
  // longer referenced.

  RogueType*   type;
  // Type info for this object.

  RogueInteger object_size;
  // Set to be ~object_size when traced through during a garbage collection,
  // then flipped back again at the end of GC.

  RogueInteger reference_count;
  // A positive reference_count ensures that this object will never be
  // collected.  A zero reference_count means this object is kept as long as
  // it is visible to the memory manager.
};

RogueObject* RogueObject_as( RogueObject* THIS, RogueType* specialized_type );
RogueLogical RogueObject_instance_of( RogueObject* THIS, RogueType* ancestor_type );
void*        RogueObject_retain( RogueObject* THIS );
void*        RogueObject_release( RogueObject* THIS );

void RogueObject_trace( void* obj );
void RogueString_trace( void* obj );
void RogueArray_trace( void* obj );

//-----------------------------------------------------------------------------
//  RogueString
//-----------------------------------------------------------------------------
struct RogueStringType : RogueType
{
};

struct RogueString : RogueObject
{
  RogueInteger   count;
  RogueInteger   hash_code;
  RogueCharacter characters[];

  static RogueString* create( int count );
  static RogueString* create( const char* c_string, int count=-1 );
  static RogueString* create( RogueCharacterList* characters );
  static void         print( RogueString* st );
  static void         print( RogueCharacter* characters, int count );

  bool                to_c_string( char* buffer, int buffer_size );
  RogueString*        update_hash_code();
};


//-----------------------------------------------------------------------------
//  RogueArray
//-----------------------------------------------------------------------------
struct RogueArrayType : RogueType
{
};

struct RogueArray : RogueObject
{
  int  count;
  int  element_size;
  bool is_reference_array;

  union
  {
    RogueObject*   objects[];
    RogueByte      logicals[];
    RogueByte      bytes[];
    RogueCharacter characters[];
    RogueInteger   integers[];
    RogueLong      longs[];
    RogueFloat     floats[];
    RogueReal      reals[];
  };

  static RogueArray* create( int count, int element_size, bool is_reference_array=false );

  RogueArray* set( RogueInteger i1, RogueArray* other, RogueInteger other_i1, RogueInteger other_i2 );
};

//-----------------------------------------------------------------------------
//  RogueProgramCore
//-----------------------------------------------------------------------------
struct RogueProgramCore
{
  RogueProgramCore();
  ~RogueProgramCore();

  RogueObject* allocate_object( RogueType* type, int size );
};


//-----------------------------------------------------------------------------
//  RogueAllocator
//-----------------------------------------------------------------------------
#ifndef ROGUEMM_PAGE_SIZE
// 4k; should be a multiple of 256 if redefined
#  define ROGUEMM_PAGE_SIZE (4*1024)
#endif

// 0 = large allocations, 1..4 = block sizes 64, 128, 192, 256
#ifndef ROGUEMM_SLOT_COUNT
#  define ROGUEMM_SLOT_COUNT 5
#endif

// 2^6 = 64
#ifndef ROGUEMM_GRANULARITY_BITS
#  define ROGUEMM_GRANULARITY_BITS 6
#endif

// Block sizes increase by 64 bytes per slot
#ifndef ROGUEMM_GRANULARITY_SIZE
#  define ROGUEMM_GRANULARITY_SIZE (1 << ROGUEMM_GRANULARITY_BITS)
#endif

// 63
#ifndef ROGUEMM_GRANULARITY_MASK
#  define ROGUEMM_GRANULARITY_MASK (ROGUEMM_GRANULARITY_SIZE - 1)
#endif

// Small allocation limit is 256 bytes - afterwards objects are allocated
// from the system.
#ifndef ROGUEMM_SMALL_ALLOCATION_SIZE_LIMIT
#  define ROGUEMM_SMALL_ALLOCATION_SIZE_LIMIT  ((ROGUEMM_SLOT_COUNT-1) << ROGUEMM_GRANULARITY_BITS)
#endif

//-----------------------------------------------------------------------------
//  RogueError
//-----------------------------------------------------------------------------
struct RogueError : RogueObject
{
};


//-----------------------------------------------------------------------------
//  RogueAllocationPage
//-----------------------------------------------------------------------------
struct RogueAllocationPage
{
  // Backs small 0..256-byte allocations.
  RogueAllocationPage* next_page;

  RogueByte* cursor;
  int        remaining;

  RogueByte  data[ ROGUEMM_PAGE_SIZE ];

  RogueAllocationPage( RogueAllocationPage* next_page );

  void* allocate( int size );
};

//-----------------------------------------------------------------------------
//  RogueAllocator
//-----------------------------------------------------------------------------
struct RogueAllocator
{
  RogueAllocationPage* pages;
  RogueObject*         objects;
  RogueObject*         available_objects[ROGUEMM_SLOT_COUNT];

  RogueAllocator();
  ~RogueAllocator();
  void* allocate( int size );
  void* free( void* data, int size );
};

RogueObject* RogueAllocator_allocate_object( RogueAllocator* THIS, RogueType* of_type, int size );
void         RogueAllocator_free_objects( RogueAllocator* THIS );
void         RogueAllocator_collect_garbage( RogueAllocator* THIS );

extern RogueAllocator    Rogue_allocator;
extern int               Rogue_type_count;
extern RogueType         Rogue_types[];
extern int               Rogue_type_info_table[];
extern int               Rogue_object_size_table[];
extern void*             Rogue_dynamic_method_table[];
extern RogueInitFn       Rogue_init_object_fn_table[];
extern RogueInitFn       Rogue_init_fn_table[];
extern RogueTraceFn      Rogue_trace_fn_table[];
extern int               Rogue_literal_string_count;
extern RogueString*      Rogue_literal_strings[];

void         Rogue_collect_garbage();


