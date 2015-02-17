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

#if defined(_WIN32)
  typedef double           RogueReal;
  typedef float            RogueFloat;
  typedef __int64          RogueLong;
  typedef __int32          RogueInteger;
  typedef unsigned __int16 RogueCharacter;
  typedef unsigned char    RogueByte;
  typedef RogueInteger     RogueLogical;
#else
  typedef double           RogueReal;
  typedef float            RogueFloat;
  typedef int64_t          RogueLong;
  typedef int32_t          RogueInteger;
  typedef uint16_t         RogueCharacter;
  typedef uint8_t          RogueByte;
  typedef RogueInteger     RogueLogical;
#endif

struct RogueAllocator;

#define ROGUE_TRACE( obj ) \
{ \
  RogueObject* _trace_obj = obj; \
  if (_trace_obj && _trace_obj->size >= 0) \
  { \
    _trace_obj->size = ~_trace_obj->size; \
    _trace_obj->type->trace( _trace_obj ); \
  } \
}

//-----------------------------------------------------------------------------
//  RogueType
//-----------------------------------------------------------------------------
struct RogueObject;

struct RogueType
{
  int         base_type_count;
  RogueType** base_types;

  int         object_size;

  RogueType();
  virtual ~RogueType();

  virtual void         configure() = 0;
  virtual RogueObject* create_object();

  RogueLogical instance_of( RogueType* ancestor_type );

  virtual const char*  name() = 0;
  virtual void         trace( RogueObject* obj ) {}
};


//-----------------------------------------------------------------------------
//  RogueObject
//-----------------------------------------------------------------------------
struct RogueObjectType : RogueType
{
  void configure();
  RogueObject* create_object();
  const char* name();
};

struct RogueObject
{
  RogueObject* next_object;
  // Used to keep track of this allocation so that it can be freed when no
  // longer referenced.

  RogueType*   type;
  // Type info for this object.

  RogueInteger size;
  // Set to be ~size when traced through during a garbage collection,
  // then flipped back again at the end of GC.

  RogueInteger reference_count;
  // A positive reference_count ensures that this object will never be
  // collected.  A zero reference_count means this object is kept as long as
  // it is visible to the memory manager.

  RogueObject* retain()  { ++reference_count; return this; }
  void         release() { --reference_count; }
};


//-----------------------------------------------------------------------------
//  RogueString
//-----------------------------------------------------------------------------
struct RogueStringType : RogueType
{
  void configure();

  RogueObject* create_object()
  {
    return 0;  // not used
  }

  const char* name() { return "String"; }
};

struct RogueString : RogueObject
{
  RogueInteger   count;
  RogueInteger   hash_code;
  RogueCharacter characters[];

  static RogueString* create( int count );
  static RogueString* create( const char* c_string, int count=-1 );
  static void         println( RogueString* st );

  RogueString* substring( RogueInteger i1, RogueInteger i2=-1 );
};


//-----------------------------------------------------------------------------
//  RogueArray
//-----------------------------------------------------------------------------
struct RogueArrayType : RogueType
{
  void configure();

  RogueObject* create_object()
  {
    return 0;  // not used
  }

  const char* name() { return "Array"; }

  void trace( RogueObject* obj );
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
  //static void         println( RogueString* st );
};


//-----------------------------------------------------------------------------
//  RogueProgramCore
//-----------------------------------------------------------------------------
struct RogueProgramCore
{
  RogueObject*  objects;
  RogueObject*  main_object;
  RogueString** literal_strings;
  int           literal_string_count;

  RogueObjectType* type_RogueObject;
  RogueStringType* type_RogueString;
  RogueArrayType*  type_RogueArray;

  RogueReal pi;

  RogueProgramCore();
  ~RogueProgramCore();

  RogueObject* allocate_object( RogueType* type, int size );

  void         collect_garbage();

  static RogueReal    mod( RogueReal a, RogueReal b );
  static RogueInteger mod( RogueInteger a, RogueInteger b );
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
  RogueObject*           free_objects[ROGUEMM_SLOT_COUNT];

  RogueAllocator();
  ~RogueAllocator();
  void* allocate( int size );
  void* allocate_permanent( int size );
  void* free( void* data, int size );
  void* free_permanent( void* data, int size );
};

extern RogueAllocator Rogue_allocator;



#include "RogueProgram.h"
