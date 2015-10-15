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

#define ROGUE_TRACE( obj ) \
{ \
  RogueObject* _trace_obj = (RogueObject*)(obj); \
  if (_trace_obj && _trace_obj->object_size >= 0) \
  { \
    _trace_obj->object_size = ~_trace_obj->object_size; \
    _trace_obj->type->trace( _trace_obj ); \
  } \
}

#define ROGUE_SINGLETON(name) (Rogue_program.type_##name->singleton())
  //THIS->standard_output = ((RogueConsole*)Rogue_program.type_Console->singleton());

#define ROGUE_PROPERTY(name) p_##name

//-----------------------------------------------------------------------------
//  RogueType
//-----------------------------------------------------------------------------
struct RogueObject;

struct RogueType
{
  int          base_type_count;
  RogueType**  base_types;

  int          index;  // used for aspect call dispatch
  int          object_size;

  RogueObject* _singleton;
  void**       methods;

  RogueType();
  virtual ~RogueType();

  virtual void         configure() = 0;
  RogueObject*         create_and_init_object() { return init_object( create_object() ); }
  virtual RogueObject* create_object();

  virtual RogueObject* init_object( RogueObject* obj ) { return obj; }
  RogueLogical instance_of( RogueType* ancestor_type );

  virtual const char*  name() = 0;
  virtual RogueObject* singleton();
  virtual void         trace( RogueObject* obj ) {}
};

//-----------------------------------------------------------------------------
//  Optional Primitive Types
//-----------------------------------------------------------------------------
struct RogueOptionalReal
{
  RogueReal value;
  RogueLogical exists;

  RogueOptionalReal() : value(0), exists(false) { }
  RogueOptionalReal( RogueReal value ) : value(value), exists(true) { }

  bool check( RogueReal* checked )
  {
    if ( !exists ) return false;
    *checked = value;
    return true;
  }


  bool operator==( const RogueOptionalReal& other ) const
  {
    if (exists)
    {
      if (other.exists) return value == other.value;
      else              return false;
    }
    else
    {
      if (other.exists) return false;
      else              return true;
    }
  }

  bool operator!=( const RogueOptionalReal& other ) const
  {
    return !(*this == other);
  }
};


struct RogueOptionalFloat
{
  RogueFloat value;
  RogueLogical exists;

  RogueOptionalFloat() : value(0), exists(false) { }
  RogueOptionalFloat( RogueFloat value ) : value(value), exists(true) { }

  bool check( RogueFloat* checked )
  {
    if ( !exists ) return false;
    *checked = value;
    return true;
  }


  bool operator==( const RogueOptionalFloat& other ) const
  {
    if (exists)
    {
      if (other.exists) return value == other.value;
      else              return false;
    }
    else
    {
      if (other.exists) return false;
      else              return true;
    }
  }

  bool operator!=( const RogueOptionalFloat& other ) const
  {
    return !(*this == other);
  }
};


struct RogueOptionalLong
{
  RogueLong value;
  RogueLogical exists;

  RogueOptionalLong() : value(0), exists(false) { }
  RogueOptionalLong( RogueLong value ) : value(value), exists(true) { }

  bool check( RogueLong* checked )
  {
    if ( !exists ) return false;
    *checked = value;
    return true;
  }


  bool operator==( const RogueOptionalLong& other ) const
  {
    if (exists)
    {
      if (other.exists) return value == other.value;
      else              return false;
    }
    else
    {
      if (other.exists) return false;
      else              return true;
    }
  }

  bool operator!=( const RogueOptionalLong& other ) const
  {
    return !(*this == other);
  }
};


struct RogueOptionalInteger
{
  RogueInteger value;
  RogueLogical exists;

  RogueOptionalInteger() : value(0), exists(false) { }
  RogueOptionalInteger( RogueInteger value ) : value(value), exists(true) { }

  bool check( RogueInteger* checked )
  {
    if ( !exists ) return false;
    *checked = value;
    return true;
  }


  bool operator==( const RogueOptionalInteger& other ) const
  {
    if (exists)
    {
      if (other.exists) return value == other.value;
      else              return false;
    }
    else
    {
      if (other.exists) return false;
      else              return true;
    }
  }

  bool operator!=( const RogueOptionalInteger& other ) const
  {
    return !(*this == other);
  }
};


struct RogueOptionalCharacter
{
  RogueCharacter value;
  RogueLogical exists;

  RogueOptionalCharacter() : value(0), exists(false) { }
  RogueOptionalCharacter( RogueCharacter value ) : value(value), exists(true) { }

  bool check( RogueCharacter* checked )
  {
    if ( !exists ) return false;
    *checked = value;
    return true;
  }


  bool operator==( const RogueOptionalCharacter& other ) const
  {
    if (exists)
    {
      if (other.exists) return value == other.value;
      else              return false;
    }
    else
    {
      if (other.exists) return false;
      else              return true;
    }
  }

  bool operator!=( const RogueOptionalCharacter& other ) const
  {
    return !(*this == other);
  }
};


struct RogueOptionalByte
{
  RogueByte value;
  RogueLogical exists;

  RogueOptionalByte() : value(0), exists(false) { }
  RogueOptionalByte( RogueByte value ) : value(value), exists(true) { }

  bool check( RogueByte* checked )
  {
    if ( !exists ) return false;
    *checked = value;
    return true;
  }


  bool operator==( const RogueOptionalByte& other ) const
  {
    if (exists)
    {
      if (other.exists) return value == other.value;
      else              return false;
    }
    else
    {
      if (other.exists) return false;
      else              return true;
    }
  }

  bool operator!=( const RogueOptionalByte& other ) const
  {
    return !(*this == other);
  }
};


struct RogueOptionalLogical
{
  RogueLogical value;
  RogueLogical exists;

  RogueOptionalLogical() : value(0), exists(false) { }
  RogueOptionalLogical( RogueLogical value ) : value(value), exists(true) { }

  bool check( RogueLogical* checked )
  {
    if ( !exists ) return false;
    *checked = value;
    return true;
  }


  bool operator==( const RogueOptionalLogical& other ) const
  {
    if (exists)
    {
      if (other.exists) return value == other.value;
      else              return false;
    }
    else
    {
      if (other.exists) return false;
      else              return true;
    }
  }

  bool operator!=( const RogueOptionalLogical& other ) const
  {
    return !(*this == other);
  }
};


struct RogueOptionalRealType : RogueType
{
  void configure() { object_size = sizeof(RogueOptionalReal); }
  const char* name() { return "Real?"; }
};

struct RogueOptionalFloatType : RogueType
{
  void configure() { object_size = sizeof(RogueOptionalFloat); }
  const char* name() { return "Float?"; }
};

struct RogueOptionalLongType : RogueType
{
  void configure() { object_size = sizeof(RogueOptionalLong); }
  const char* name() { return "Long?"; }
};

struct RogueOptionalIntegerType : RogueType
{
  void configure() { object_size = sizeof(RogueOptionalInteger); }
  const char* name() { return "Integer?"; }
};

struct RogueOptionalCharacterType : RogueType
{
  void configure() { object_size = sizeof(RogueOptionalCharacter); }
  const char* name() { return "Character?"; }
};

struct RogueOptionalByteType : RogueType
{
  void configure() { object_size = sizeof(RogueOptionalByte); }
  const char* name() { return "Byte?"; }
};

struct RogueOptionalLogicalType : RogueType
{
  void configure() { object_size = sizeof(RogueOptionalLogical); }
  const char* name() { return "Logical?"; }
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

  RogueInteger object_size;
  // Set to be ~object_size when traced through during a garbage collection,
  // then flipped back again at the end of GC.

  RogueInteger reference_count;
  // A positive reference_count ensures that this object will never be
  // collected.  A zero reference_count means this object is kept as long as
  // it is visible to the memory manager.

  RogueObject* retain()  { ++reference_count; return this; }
  void         release() { --reference_count; }

  static RogueObject* as( RogueObject* object, RogueType* specialized_type );
  static RogueLogical instance_of( RogueObject* object, RogueType* ancestor_type );
};


//-----------------------------------------------------------------------------
//  RogueString
//-----------------------------------------------------------------------------
struct RogueStringType : RogueType
{
  void configure();

  const char* name() { return "String"; }
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
  void configure();

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

  RogueArray* set( RogueInteger i1, RogueArray* other, RogueInteger other_i1, RogueInteger other_i2 );
};

//-----------------------------------------------------------------------------
//  RogueProgramCore
//-----------------------------------------------------------------------------
#define ROGUE_BUILT_IN_TYPE_COUNT 10

struct RogueProgramCore
{
  RogueObject*  objects;
  RogueObject*  main_object;
  RogueString** literal_strings;
  int           literal_string_count;

  RogueType**   types;
  int           type_count;
  int           next_type_index;

  RogueOptionalRealType*      type_OptionalReal;
  RogueOptionalFloatType*     type_OptionalFloat;
  RogueOptionalLongType*      type_OptionalLong;
  RogueOptionalIntegerType*   type_OptionalInteger;
  RogueOptionalCharacterType* type_OptionalCharacter;
  RogueOptionalByteType*      type_OptionalByte;
  RogueOptionalLogicalType*   type_OptionalLogical;

  RogueObjectType* type_Object;
  RogueStringType* type_String;
  RogueArrayType*  type_Array;

  // NOTE: increment ROGUE_BUILT_IN_TYPE_COUNT when adding new built-in types

  RogueReal pi;

  RogueProgramCore( int type_count );
  ~RogueProgramCore();

  RogueObject* allocate_object( RogueType* type, int size );

  void         collect_garbage();

  static RogueReal    mod( RogueReal a, RogueReal b );
  static RogueInteger mod( RogueInteger a, RogueInteger b );
  static RogueLong    mod( RogueLong a, RogueLong b );
  static RogueInteger shift_right( RogueInteger value, RogueInteger bits );
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

//=============================================================================
//  Various Native Methods
//=============================================================================

//-----------------------------------------------------------------------------
//  RogueError
//-----------------------------------------------------------------------------
struct RogueError : RogueObject
{
};

