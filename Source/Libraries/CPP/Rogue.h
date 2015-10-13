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
struct RogueFileReaderType;
struct RogueFileWriterType;

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
//  RogueSystemList
//-----------------------------------------------------------------------------
template <class DataType>
struct RogueSystemList
{
  DataType* data;
  int count;
  int capacity;

  RogueSystemList( int capacity=10 ) : count(0)
  {
    this->capacity = capacity;
    data = new DataType[capacity];
    memset( data, 0, sizeof(DataType)*capacity );
    count = 0;
  }

  ~RogueSystemList()
  {
    delete data;
    data = 0;
    count = 0;
    capacity = 0;
  }

  RogueSystemList* add( DataType value )
  {
    if (count == capacity) ensure_capacity( capacity ? capacity*2 : 10 );
    data[count++] = value;
    return this;
  }

  RogueSystemList* clear() { count = 0; return this; }

  RogueSystemList* discard( int i1, int i2 )
  {
    if (i1 < 0)      i1 = 0;
    if (i2 >= count) i2 = count - 1;

    if (i1 > i2) return this;

    if (i2 == count-1)
    {
      if (i1 == 0) clear();
      else         count = i1;
      return this;
    }

    memmove( data+i1, data+i2+1, (count-(i2+1)) * sizeof(DataType) );
    count -= (i2-i1) + 1;
    return this;
  }

  RogueSystemList* discard_from( int i1 )
  {
    return discard( i1, count-1 );
  }

  inline DataType& operator[]( int index ) { return data[index]; }

  void remove( DataType value )
  {
    for (int i=count-1; i>=0; --i)
    {
      if (data[i] == value)
      {
        remove_at(i);
        return;
      }
    }
  }

  DataType remove_at( int index )
  {
    if (index == count-1)
    {
      return data[--count];
    }
    else
    {
      DataType result = data[index];
      --count;
      while (index < count)
      {
        data[index] = data[index+1];
        ++index;
      }
      return result;
    }
  }

  DataType remove_last()
  {
    return data[ --count ];
  }

  RogueSystemList* reserve( int additional_count )
  {
    return ensure_capacity( count + additional_count );
  }

  RogueSystemList* ensure_capacity( int c )
  {
    if (capacity >= c) return this;

    if (capacity > 0)
    {
      int double_capacity = (capacity << 1);
      if (double_capacity > c) c = double_capacity;
    }

    capacity = c;

    int bytes = sizeof(DataType) * capacity;

    if ( !data )
    {
      data = new DataType[capacity];
      memset( data, 0, sizeof(DataType) * capacity );
    }
    else
    {
      int old_bytes = sizeof(DataType) * count;

      DataType* new_data = new DataType[capacity];
      memset( ((char*)new_data) + old_bytes, 0, bytes - old_bytes );
      memcpy( new_data, data, old_bytes );

      delete data;
      data = new_data;
    }

    return this;
  }

};


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
//  Primitive Types
//-----------------------------------------------------------------------------
struct RogueRealType : RogueType
{
  void configure() { object_size = sizeof(RogueReal); }
  const char* name() { return "Real"; }
};

struct RogueFloatType : RogueType
{
  void configure() { object_size = sizeof(RogueFloat); }
  const char* name() { return "Float"; }
};

struct RogueLongType : RogueType
{
  void configure() { object_size = sizeof(RogueLong); }
  const char* name() { return "Long"; }
};

struct RogueIntegerType : RogueType
{
  void configure() { object_size = sizeof(RogueInteger); }
  const char* name() { return "Integer"; }
};

struct RogueCharacterType : RogueType
{
  void configure() { object_size = sizeof(RogueCharacter); }
  const char* name() { return "Character"; }
};

struct RogueByteType : RogueType
{
  void configure() { object_size = sizeof(RogueByte); }
  const char* name() { return "Byte"; }
};

struct RogueLogicalType : RogueType
{
  void configure() { object_size = sizeof(RogueLogical); }
  const char* name() { return "Logical"; }
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

  RogueInteger compare_to( RogueString* other );
  RogueLogical contains( RogueString* substring, RogueInteger at_index );
  RogueOptionalInteger locate( RogueCharacter ch, RogueOptionalInteger optional_i1 );
  RogueOptionalInteger locate( RogueString* other, RogueOptionalInteger optional_i1 );
  RogueOptionalInteger locate_last( RogueCharacter ch, RogueOptionalInteger i1 );
  RogueOptionalInteger locate_last( RogueString* other, RogueOptionalInteger i1 );
  RogueString* plus( const char* c_str );
  RogueString* plus( char ch );
  RogueString* plus( RogueCharacter ch );
  RogueString* plus( RogueInteger value );
  RogueString* plus( RogueLong value );
  RogueString* plus( RogueReal value );
  RogueString* plus( RogueString* other );
  bool         to_c_string( char* buffer, int buffer_size );
  RogueInteger to_integer();
  RogueReal    to_real();
  RogueString* update_hash_code();
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
#define ROGUE_BUILT_IN_TYPE_COUNT 19

struct RogueProgramCore
{
  RogueObject*  objects;
  RogueObject*  main_object;
  RogueString** literal_strings;
  int           literal_string_count;

  RogueType**   types;
  int           type_count;
  int           next_type_index;

  RogueRealType*      type_Real;
  RogueFloatType*     type_Float;
  RogueLongType*      type_Long;
  RogueIntegerType*   type_Integer;
  RogueCharacterType* type_Character;
  RogueByteType*      type_Byte;
  RogueLogicalType*   type_Logical;

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

  RogueFileReaderType*  type_FileReader;
  RogueFileWriterType*  type_FileWriter;

  // NOTE: increment ROGUE_BUILT_IN_TYPE_COUNT when adding new built-in types

  RogueReal pi;

  RogueProgramCore( int type_count );
  ~RogueProgramCore();

  RogueObject* allocate_object( RogueType* type, int size );

  void         collect_garbage();

  static RogueReal    mod( RogueReal a, RogueReal b );
  static RogueInteger mod( RogueInteger a, RogueInteger b );
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

//-----------------------------------------------------------------------------
//  Console
//-----------------------------------------------------------------------------
RogueString* RogueConsole__input( RogueString* prompt );

//-----------------------------------------------------------------------------
//  File
//-----------------------------------------------------------------------------
RogueString* RogueFile__absolute_filepath( RogueString* filepath );
RogueLogical RogueFile__exists( RogueString* filepath );
RogueLogical RogueFile__is_folder( RogueString* filepath );
RogueString* RogueFile__load( RogueString* filepath );
RogueLogical RogueFile__save( RogueString* filepath, RogueString* data );
RogueInteger RogueFile__size( RogueString* filepath );

//-----------------------------------------------------------------------------
//  FileReader
//-----------------------------------------------------------------------------
struct RogueFileReaderType : RogueType
{
  void configure();

  const char* name() { return "FileReader"; }
  void        trace( RogueObject* obj );
};

struct RogueFileReader : RogueObject
{
  FILE* fp;
  RogueString*   filepath;
  unsigned char  buffer[1024];
  int            buffer_count;
  int            buffer_position;
  int            count;
  int            position;
};

RogueFileReader* RogueFileReader__create( RogueString* filepath );
RogueFileReader* RogueFileReader__close( RogueFileReader* reader );
RogueInteger     RogueFileReader__count( RogueFileReader* reader );
RogueLogical     RogueFileReader__has_another( RogueFileReader* reader );
RogueLogical     RogueFileReader__open( RogueFileReader* reader, RogueString* filepath );
RogueCharacter   RogueFileReader__peek( RogueFileReader* reader );
RogueInteger     RogueFileReader__position( RogueFileReader* reader );
RogueCharacter   RogueFileReader__read( RogueFileReader* reader );
RogueFileReader* RogueFileReader__set_position( RogueFileReader* reader, RogueInteger new_position );


//-----------------------------------------------------------------------------
//  FileWriter
//-----------------------------------------------------------------------------
struct RogueFileWriterType : RogueType
{
  void configure();

  const char* name() { return "FileWriter"; }
  void        trace( RogueObject* obj );
};

struct RogueFileWriter : RogueObject
{
  FILE* fp;
  RogueString*   filepath;
  unsigned char  buffer[1024];
  int            buffer_position;
  int            count;
};

RogueFileWriter* RogueFileWriter__create( RogueString* filepath );
RogueFileWriter* RogueFileWriter__close( RogueFileWriter* writer );
RogueInteger     RogueFileWriter__count( RogueFileWriter* writer );
RogueFileWriter* RogueFileWriter__flush( RogueFileWriter* writer );
RogueLogical     RogueFileWriter__open( RogueFileWriter* writer, RogueString* filepath );
RogueFileWriter* RogueFileWriter__write( RogueFileWriter* writer, RogueCharacter ch );


//-----------------------------------------------------------------------------
//  Real
//-----------------------------------------------------------------------------
RogueReal    RogueReal__create( RogueInteger high_bits, RogueInteger low_bits );
RogueInteger RogueReal__high_bits( RogueReal THIS );
RogueInteger RogueReal__low_bits( RogueReal THIS );


//-----------------------------------------------------------------------------
//  StringBuilder
//-----------------------------------------------------------------------------
struct RogueStringBuilder;

RogueStringBuilder* RogueStringBuilder__reserve( RogueStringBuilder* buffer, RogueInteger additional_count );
RogueStringBuilder* RogueStringBuilder__print( RogueStringBuilder* buffer, const char* st );
RogueStringBuilder* RogueStringBuilder__print( RogueStringBuilder* buffer, RogueInteger value );
RogueStringBuilder* RogueStringBuilder__print( RogueStringBuilder* buffer, RogueLong value );
RogueStringBuilder* RogueStringBuilder__print( RogueStringBuilder* buffer, RogueReal value, RogueInteger decimal_places );

//-----------------------------------------------------------------------------
//  System
//-----------------------------------------------------------------------------
void RogueSystem__exit( int result_code );

//-----------------------------------------------------------------------------
//  Time
//-----------------------------------------------------------------------------
RogueReal RogueTime__current();

