//=============================================================================
// Embedded copy of Rogue.h
//=============================================================================
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
//  RogueSystemMessageQueue
//-----------------------------------------------------------------------------
struct RogueSystemMessageQueue
{
  RogueSystemList<RogueByte>* write_list;
  RogueSystemList<RogueByte>* read_list;
  int read_position;
  int remaining_bytes_in_current;
  int message_size_location;

  RogueSystemMessageQueue();
  ~RogueSystemMessageQueue();

  RogueSystemMessageQueue* begin_message( const char* message_type );
  void                     begin_reading();
  bool                     has_another();

  RogueByte                read_byte();
  RogueCharacter           read_character();
  RogueFloat               read_float();
  RogueInteger             read_int_x();
  RogueInteger             read_integer();
  RogueLogical             read_logical();
  RogueLong                read_long();
  RogueReal                read_real();
  int                      read_string( char* buffer, int buffer_size );
  char*                    read_new_c_string();
  RogueString*             read_string();

  RogueSystemMessageQueue* write_byte( int value );
  RogueSystemMessageQueue* write_character( int value );
  RogueSystemMessageQueue* write_float( float value);
  RogueSystemMessageQueue* write_int_x( int value );
  RogueSystemMessageQueue* write_integer( int value );
  RogueSystemMessageQueue* write_logical( bool value );
  RogueSystemMessageQueue* write_long( RogueLong value );
  RogueSystemMessageQueue* write_real( double value );
  RogueSystemMessageQueue* write_string( const char* value );
  RogueSystemMessageQueue* write_string( RogueCharacter* value, int count );

  // INTERNAL USE
  void update_message_size();
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
  RogueString* substring( RogueInteger i1 );
  RogueString* substring( RogueInteger i1, RogueInteger i2 );
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
  RogueSystemMessageQueue event_queue;

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

//=============================================================================
// Code generated from Rogue source
//=============================================================================
#include <cmath>

struct RogueTypeCharacterList;
struct RogueTypeGenericList;
struct RogueTypeStringBuilder;
struct RogueTypeStringList;
struct RogueTypeGlobal;
struct RogueTypeConsole;
struct RogueTypeError;
struct RogueTypeRogueC;
struct RogueTypeString_LogicalTable;
struct RogueTypeSystem;
struct RogueTypeFile;
struct RogueTypeProgram;
struct RogueTypeToken;
struct RogueTypeRogueError;
struct RogueTypeStringListOps;
struct RogueTypeParser;
struct RogueTypeTokenList;
struct RogueTypeTemplateList;
struct RogueTypeTemplate;
struct RogueTypeString_TemplateTable;
struct RogueTypeType;
struct RogueTypeTypeList;
struct RogueTypeString_TypeTable;
struct RogueTypeString_IntegerTable;
struct RogueTypeAttribute;
struct RogueTypeAttributes;
struct RogueTypeMethod;
struct RogueTypeCmd;
struct RogueTypeCmdReturn;
struct RogueTypeCmdStatement;
struct RogueTypeCmdStatementList;
struct RogueTypeCmdList;
struct RogueTypeTokenType;
struct RogueTypeMethodList;
struct RogueTypeCPPWriter;
struct RogueTypeCharacterReader;
struct RogueTypeString_MethodTable;
struct RogueTypeLocalList;
struct RogueTypeLocal;
struct RogueTypeTaskManager;
struct RogueTypeTask;
struct RogueTypeTaskList;
struct RogueTypeTaskManager__await_all__task127;
struct RogueTypeSystemEventQueue;
struct RogueTypeEventManager;
struct RogueTypeRandom;
struct RogueTypeCharacterListOps;
struct RogueTypeString_LogicalTableEntryList;
struct RogueTypeString_LogicalTableEntry;
struct RogueTypeByteList;
struct RogueTypeTokenReader;
struct RogueTypePropertyList;
struct RogueTypeProperty;
struct RogueTypeTokenizer;
struct RogueTypeParseReader;
struct RogueTypePreprocessor;
struct RogueTypeString_TokenTypeTable;
struct RogueTypeLiteralCharacterToken;
struct RogueTypeLiteralIntegerToken;
struct RogueTypeLiteralRealToken;
struct RogueTypeLiteralStringToken;
struct RogueTypeTypeIdentifierToken;
struct RogueTypeTypeParameter;
struct RogueTypeCmdLiteralInteger;
struct RogueTypeCmdLiteral;
struct RogueTypeCloneArgs;
struct RogueTypeCmdArgs;
struct RogueTypeCmdAdd;
struct RogueTypeCmdBinary;
struct RogueTypeString_CmdTable;
struct RogueTypeCmdIf;
struct RogueTypeCmdControlStructure;
struct RogueTypeCmdWhich;
struct RogueTypeCmdContingent;
struct RogueTypeCmdGenericLoop;
struct RogueTypeCmdTry;
struct RogueTypeCmdAwait;
struct RogueTypeCmdYield;
struct RogueTypeCmdThrow;
struct RogueTypeCmdTrace;
struct RogueTypeCmdEscape;
struct RogueTypeCmdNextIteration;
struct RogueTypeCmdNecessary;
struct RogueTypeCmdSufficient;
struct RogueTypeCmdAdjust;
struct RogueTypeCmdAssign;
struct RogueTypeCmdOpWithAssign;
struct RogueTypeCmdAccess;
struct RogueTypeCmdWhichCaseList;
struct RogueTypeCmdWhichCase;
struct RogueTypeCmdCatchList;
struct RogueTypeCmdCatch;
struct RogueTypeCmdLocalDeclaration;
struct RogueTypeCmdAdjustLocal;
struct RogueTypeCmdReadLocal;
struct RogueTypeCmdCompareLE;
struct RogueTypeCmdComparison;
struct RogueTypeCmdRange;
struct RogueTypeCmdLocalOpWithAssign;
struct RogueTypeCmdResolvedOpWithAssign;
struct RogueTypeCmdForEach;
struct RogueTypeCmdRangeUpTo;
struct RogueTypeCmdLogicalXor;
struct RogueTypeCmdBinaryLogical;
struct RogueTypeCmdLogicalOr;
struct RogueTypeCmdLogicalAnd;
struct RogueTypeCmdCompareEQ;
struct RogueTypeCmdCompareIs;
struct RogueTypeCmdCompareNE;
struct RogueTypeCmdCompareIsNot;
struct RogueTypeCmdCompareLT;
struct RogueTypeCmdCompareGT;
struct RogueTypeCmdCompareGE;
struct RogueTypeCmdInstanceOf;
struct RogueTypeCmdTypeOperator;
struct RogueTypeCmdLogicalNot;
struct RogueTypeCmdUnary;
struct RogueTypeCmdBitwiseXor;
struct RogueTypeCmdBitwiseOp;
struct RogueTypeCmdBitwiseOr;
struct RogueTypeCmdBitwiseAnd;
struct RogueTypeCmdBitwiseShiftLeft;
struct RogueTypeCmdBitwiseShiftRight;
struct RogueTypeCmdBitwiseShiftRightX;
struct RogueTypeCmdSubtract;
struct RogueTypeCmdMultiply;
struct RogueTypeCmdDivide;
struct RogueTypeCmdMod;
struct RogueTypeCmdPower;
struct RogueTypeCmdNegate;
struct RogueTypeCmdBitwiseNot;
struct RogueTypeCmdLogicalize;
struct RogueTypeCmdGetOptionalValue;
struct RogueTypeCmdElementAccess;
struct RogueTypeCmdConvertToType;
struct RogueTypeCmdCreateCallback;
struct RogueTypeCmdAs;
struct RogueTypeCmdDefaultValue;
struct RogueTypeCmdFormattedString;
struct RogueTypeCmdLiteralString;
struct RogueTypeCmdLiteralNull;
struct RogueTypeCmdLiteralReal;
struct RogueTypeCmdLiteralCharacter;
struct RogueTypeCmdLiteralThis;
struct RogueTypeCmdThisContext;
struct RogueTypeCmdLiteralLogical;
struct RogueTypeCmdCreateList;
struct RogueTypeCmdCallPriorMethod;
struct RogueTypeFnParamList;
struct RogueTypeFnParam;
struct RogueTypeFnArgList;
struct RogueTypeFnArg;
struct RogueTypeCmdCreateFunction;
struct RogueTypeTokenListOps;
struct RogueTypeTemplateListOps;
struct RogueTypeTypeParameterList;
struct RogueTypeString_TypeSpecializerTable;
struct RogueTypeTypeSpecializer;
struct RogueTypeString_TemplateTableEntryList;
struct RogueTypeString_TemplateTableEntry;
struct RogueTypeString_PropertyTable;
struct RogueTypeString_MethodListTable;
struct RogueTypeCmdCreateCompound;
struct RogueTypeCmdWriteSetting;
struct RogueTypeMethodListOps;
struct RogueTypeCmdWriteProperty;
struct RogueTypeTypeListOps;
struct RogueTypePropertyListOps;
struct RogueTypeString_TypeTableEntryList;
struct RogueTypeString_TypeTableEntry;
struct RogueTypeString_IntegerTableEntryList;
struct RogueTypeString_IntegerTableEntry;
struct RogueTypeScope;
struct RogueTypeCmdControlStructureList;
struct RogueTypeTaskArgs;
struct RogueTypeCmdTaskControl;
struct RogueTypeCmdTaskControlSection;
struct RogueTypeCmdCastToType;
struct RogueTypeCmdCreateOptionalValue;
struct RogueTypeCmdListOps;
struct RogueTypeString_MethodTableEntryList;
struct RogueTypeString_MethodTableEntry;
struct RogueTypeLocalListOps;
struct RogueTypeTaskListOps;
struct RogueTypeTime;
struct RogueTypeString_LogicalTableEntryListOps;
struct RogueTypeByteListOps;
struct RogueTypeDirectiveTokenType;
struct RogueTypeEOLTokenType;
struct RogueTypeStructureTokenType;
struct RogueTypeOpWithAssignTokenType;
struct RogueTypeEOLToken;
struct RogueTypeString_TokenListTable;
struct RogueTypePreprocessorTokenReader;
struct RogueTypeString_TokenTypeTableEntryList;
struct RogueTypeString_TokenTypeTableEntry;
struct RogueTypeString_CmdTableEntryList;
struct RogueTypeString_CmdTableEntry;
struct RogueTypeCmdTaskControlSectionList;
struct RogueTypeCmdBlock;
struct RogueTypeCmdReadProperty;
struct RogueTypeCmdWriteLocal;
struct RogueTypeInlineArgs;
struct RogueTypeCmdReadSingleton;
struct RogueTypeCmdCreateArray;
struct RogueTypeCmdCallRoutine;
struct RogueTypeCmdCall;
struct RogueTypeCmdCreateObject;
struct RogueTypeCmdReadSetting;
struct RogueTypeCmdLogicalizeOptionalValue;
struct RogueTypeCmdOpAssignSetting;
struct RogueTypeCmdOpAssignProperty;
struct RogueTypeCmdWhichCaseListOps;
struct RogueTypeCmdCatchListOps;
struct RogueTypeCmdReadArrayElement;
struct RogueTypeCmdWriteArrayElement;
struct RogueTypeCmdConvertToPrimitiveType;
struct RogueTypeFnParamListOps;
struct RogueTypeFnArgListOps;
struct RogueTypeTypeParameterListOps;
struct RogueTypeString_TypeSpecializerTableEntryList;
struct RogueTypeString_TypeSpecializerTableEntry;
struct RogueTypeString_TemplateTableEntryListOps;
struct RogueTypeString_PropertyTableEntryList;
struct RogueTypeString_PropertyTableEntry;
struct RogueTypeString_MethodListTableEntryList;
struct RogueTypeString_MethodListTableEntry;
struct RogueTypeString_TypeTableEntryListOps;
struct RogueTypeString_IntegerTableEntryListOps;
struct RogueTypeCmdCallInlineNativeRoutine;
struct RogueTypeCmdCallInlineNative;
struct RogueTypeCmdCallNativeRoutine;
struct RogueTypeCmdReadArrayCount;
struct RogueTypeCmdCallInlineNativeMethod;
struct RogueTypeCmdCallNativeMethod;
struct RogueTypeCmdCallAspectMethod;
struct RogueTypeCmdCallDynamicMethod;
struct RogueTypeCmdCallMethod;
struct RogueTypeCandidateMethods;
struct RogueTypeCmdControlStructureListOps;
struct RogueTypeString_MethodTableEntryListOps;
struct RogueTypeString_TokenListTableEntryList;
struct RogueTypeString_TokenListTableEntry;
struct RogueTypeString_TokenTypeTableEntryListOps;
struct RogueTypeString_CmdTableEntryListOps;
struct RogueTypeCmdTaskControlSectionListOps;
struct RogueTypeCmdAdjustProperty;
struct RogueTypeString_TypeSpecializerTableEntryListOps;
struct RogueTypeString_PropertyTableEntryListOps;
struct RogueTypeString_MethodListTableEntryListOps;
struct RogueTypeCmdCallStaticMethod;
struct RogueTypeString_TokenListTableEntryListOps;

struct RogueCharacterList;
struct RogueClassGenericList;
struct RogueStringBuilder;
struct RogueStringList;
struct RogueClassGlobal;
struct RogueClassConsole;
struct RogueClassError;
struct RogueClassRogueC;
struct RogueClassString_LogicalTable;
struct RogueClassSystem;
struct RogueClassFile;
struct RogueClassProgram;
struct RogueClassToken;
struct RogueClassRogueError;
struct RogueClassStringListOps;
struct RogueClassParser;
struct RogueTokenList;
struct RogueTemplateList;
struct RogueClassTemplate;
struct RogueClassString_TemplateTable;
struct RogueClassType;
struct RogueTypeList;
struct RogueClassString_TypeTable;
struct RogueClassString_IntegerTable;
struct RogueClassAttribute;
struct RogueClassAttributes;
struct RogueClassMethod;
struct RogueClassCmd;
struct RogueClassCmdReturn;
struct RogueClassCmdStatement;
struct RogueClassCmdStatementList;
struct RogueCmdList;
struct RogueClassTokenType;
struct RogueMethodList;
struct RogueClassCPPWriter;
struct RogueClassCharacterReader;
struct RogueClassString_MethodTable;
struct RogueLocalList;
struct RogueClassLocal;
struct RogueClassTaskManager;
struct RogueClassTask;
struct RogueTaskList;
struct RogueClassTaskManager__await_all__task127;
struct RogueClassSystemEventQueue;
struct RogueClassEventManager;
struct RogueClassRandom;
struct RogueClassCharacterListOps;
struct RogueString_LogicalTableEntryList;
struct RogueClassString_LogicalTableEntry;
struct RogueByteList;
struct RogueClassTokenReader;
struct RoguePropertyList;
struct RogueClassProperty;
struct RogueClassTokenizer;
struct RogueClassParseReader;
struct RogueClassPreprocessor;
struct RogueClassString_TokenTypeTable;
struct RogueClassLiteralCharacterToken;
struct RogueClassLiteralIntegerToken;
struct RogueClassLiteralRealToken;
struct RogueClassLiteralStringToken;
struct RogueClassTypeIdentifierToken;
struct RogueClassTypeParameter;
struct RogueClassCmdLiteralInteger;
struct RogueClassCmdLiteral;
struct RogueClassCloneArgs;
struct RogueClassCmdArgs;
struct RogueClassCmdAdd;
struct RogueClassCmdBinary;
struct RogueClassString_CmdTable;
struct RogueClassCmdIf;
struct RogueClassCmdControlStructure;
struct RogueClassCmdWhich;
struct RogueClassCmdContingent;
struct RogueClassCmdGenericLoop;
struct RogueClassCmdTry;
struct RogueClassCmdAwait;
struct RogueClassCmdYield;
struct RogueClassCmdThrow;
struct RogueClassCmdTrace;
struct RogueClassCmdEscape;
struct RogueClassCmdNextIteration;
struct RogueClassCmdNecessary;
struct RogueClassCmdSufficient;
struct RogueClassCmdAdjust;
struct RogueClassCmdAssign;
struct RogueClassCmdOpWithAssign;
struct RogueClassCmdAccess;
struct RogueCmdWhichCaseList;
struct RogueClassCmdWhichCase;
struct RogueCmdCatchList;
struct RogueClassCmdCatch;
struct RogueClassCmdLocalDeclaration;
struct RogueClassCmdAdjustLocal;
struct RogueClassCmdReadLocal;
struct RogueClassCmdCompareLE;
struct RogueClassCmdComparison;
struct RogueClassCmdRange;
struct RogueClassCmdLocalOpWithAssign;
struct RogueClassCmdResolvedOpWithAssign;
struct RogueClassCmdForEach;
struct RogueClassCmdRangeUpTo;
struct RogueClassCmdLogicalXor;
struct RogueClassCmdBinaryLogical;
struct RogueClassCmdLogicalOr;
struct RogueClassCmdLogicalAnd;
struct RogueClassCmdCompareEQ;
struct RogueClassCmdCompareIs;
struct RogueClassCmdCompareNE;
struct RogueClassCmdCompareIsNot;
struct RogueClassCmdCompareLT;
struct RogueClassCmdCompareGT;
struct RogueClassCmdCompareGE;
struct RogueClassCmdInstanceOf;
struct RogueClassCmdTypeOperator;
struct RogueClassCmdLogicalNot;
struct RogueClassCmdUnary;
struct RogueClassCmdBitwiseXor;
struct RogueClassCmdBitwiseOp;
struct RogueClassCmdBitwiseOr;
struct RogueClassCmdBitwiseAnd;
struct RogueClassCmdBitwiseShiftLeft;
struct RogueClassCmdBitwiseShiftRight;
struct RogueClassCmdBitwiseShiftRightX;
struct RogueClassCmdSubtract;
struct RogueClassCmdMultiply;
struct RogueClassCmdDivide;
struct RogueClassCmdMod;
struct RogueClassCmdPower;
struct RogueClassCmdNegate;
struct RogueClassCmdBitwiseNot;
struct RogueClassCmdLogicalize;
struct RogueClassCmdGetOptionalValue;
struct RogueClassCmdElementAccess;
struct RogueClassCmdConvertToType;
struct RogueClassCmdCreateCallback;
struct RogueClassCmdAs;
struct RogueClassCmdDefaultValue;
struct RogueClassCmdFormattedString;
struct RogueClassCmdLiteralString;
struct RogueClassCmdLiteralNull;
struct RogueClassCmdLiteralReal;
struct RogueClassCmdLiteralCharacter;
struct RogueClassCmdLiteralThis;
struct RogueClassCmdThisContext;
struct RogueClassCmdLiteralLogical;
struct RogueClassCmdCreateList;
struct RogueClassCmdCallPriorMethod;
struct RogueFnParamList;
struct RogueClassFnParam;
struct RogueFnArgList;
struct RogueClassFnArg;
struct RogueClassCmdCreateFunction;
struct RogueClassTokenListOps;
struct RogueClassTemplateListOps;
struct RogueTypeParameterList;
struct RogueClassString_TypeSpecializerTable;
struct RogueClassTypeSpecializer;
struct RogueString_TemplateTableEntryList;
struct RogueClassString_TemplateTableEntry;
struct RogueClassString_PropertyTable;
struct RogueClassString_MethodListTable;
struct RogueClassCmdCreateCompound;
struct RogueClassCmdWriteSetting;
struct RogueClassMethodListOps;
struct RogueClassCmdWriteProperty;
struct RogueClassTypeListOps;
struct RogueClassPropertyListOps;
struct RogueString_TypeTableEntryList;
struct RogueClassString_TypeTableEntry;
struct RogueString_IntegerTableEntryList;
struct RogueClassString_IntegerTableEntry;
struct RogueClassScope;
struct RogueCmdControlStructureList;
struct RogueClassTaskArgs;
struct RogueClassCmdTaskControl;
struct RogueClassCmdTaskControlSection;
struct RogueClassCmdCastToType;
struct RogueClassCmdCreateOptionalValue;
struct RogueClassCmdListOps;
struct RogueString_MethodTableEntryList;
struct RogueClassString_MethodTableEntry;
struct RogueClassLocalListOps;
struct RogueClassTaskListOps;
struct RogueClassTime;
struct RogueClassString_LogicalTableEntryListOps;
struct RogueClassByteListOps;
struct RogueClassDirectiveTokenType;
struct RogueClassEOLTokenType;
struct RogueClassStructureTokenType;
struct RogueClassOpWithAssignTokenType;
struct RogueClassEOLToken;
struct RogueClassString_TokenListTable;
struct RogueClassPreprocessorTokenReader;
struct RogueString_TokenTypeTableEntryList;
struct RogueClassString_TokenTypeTableEntry;
struct RogueString_CmdTableEntryList;
struct RogueClassString_CmdTableEntry;
struct RogueCmdTaskControlSectionList;
struct RogueClassCmdBlock;
struct RogueClassCmdReadProperty;
struct RogueClassCmdWriteLocal;
struct RogueClassInlineArgs;
struct RogueClassCmdReadSingleton;
struct RogueClassCmdCreateArray;
struct RogueClassCmdCallRoutine;
struct RogueClassCmdCall;
struct RogueClassCmdCreateObject;
struct RogueClassCmdReadSetting;
struct RogueClassCmdLogicalizeOptionalValue;
struct RogueClassCmdOpAssignSetting;
struct RogueClassCmdOpAssignProperty;
struct RogueClassCmdWhichCaseListOps;
struct RogueClassCmdCatchListOps;
struct RogueClassCmdReadArrayElement;
struct RogueClassCmdWriteArrayElement;
struct RogueClassCmdConvertToPrimitiveType;
struct RogueClassFnParamListOps;
struct RogueClassFnArgListOps;
struct RogueClassTypeParameterListOps;
struct RogueString_TypeSpecializerTableEntryList;
struct RogueClassString_TypeSpecializerTableEntry;
struct RogueClassString_TemplateTableEntryListOps;
struct RogueString_PropertyTableEntryList;
struct RogueClassString_PropertyTableEntry;
struct RogueString_MethodListTableEntryList;
struct RogueClassString_MethodListTableEntry;
struct RogueClassString_TypeTableEntryListOps;
struct RogueClassString_IntegerTableEntryListOps;
struct RogueClassCmdCallInlineNativeRoutine;
struct RogueClassCmdCallInlineNative;
struct RogueClassCmdCallNativeRoutine;
struct RogueClassCmdReadArrayCount;
struct RogueClassCmdCallInlineNativeMethod;
struct RogueClassCmdCallNativeMethod;
struct RogueClassCmdCallAspectMethod;
struct RogueClassCmdCallDynamicMethod;
struct RogueClassCmdCallMethod;
struct RogueClassCandidateMethods;
struct RogueClassCmdControlStructureListOps;
struct RogueClassString_MethodTableEntryListOps;
struct RogueString_TokenListTableEntryList;
struct RogueClassString_TokenListTableEntry;
struct RogueClassString_TokenTypeTableEntryListOps;
struct RogueClassString_CmdTableEntryListOps;
struct RogueClassCmdTaskControlSectionListOps;
struct RogueClassCmdAdjustProperty;
struct RogueClassString_TypeSpecializerTableEntryListOps;
struct RogueClassString_PropertyTableEntryListOps;
struct RogueClassString_MethodListTableEntryListOps;
struct RogueClassCmdCallStaticMethod;
struct RogueClassString_TokenListTableEntryListOps;


struct RogueCharacterList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassGenericList : RogueObject
{
  // PROPERTIES

};

struct RogueStringBuilder : RogueObject
{
  // PROPERTIES
  RogueCharacterList* characters;
  RogueInteger indent;
  RogueLogical at_newline;

};

struct RogueStringList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassGlobal : RogueObject
{
  // PROPERTIES
  RogueStringBuilder* global_output_buffer;
  RogueClassConsole* standard_output;

};

struct RogueClassConsole : RogueObject
{
  // PROPERTIES

};

struct RogueClassError : RogueObject
{
  // PROPERTIES
  RogueString* message;

};

struct RogueClassRogueC : RogueObject
{
  // PROPERTIES
  RogueStringList* included_files;
  RogueStringList* prefix_path_list;
  RogueClassString_LogicalTable* prefix_path_lookup;
  RogueClassString_LogicalTable* compile_targets;
  RogueString* libraries_folder;
  RogueStringList* source_files;
  RogueLogical generate_main;
  RogueString* first_filepath;
  RogueString* output_filepath;
  RogueString* target;
  RogueString* execute_args;

};

struct RogueClassString_LogicalTable : RogueObject
{
  // PROPERTIES
  RogueInteger bin_mask;
  RogueString_LogicalTableEntryList* bins;
  RogueStringList* keys;

};

struct RogueClassSystem : RogueObject
{
  // SETTINGS
  static RogueStringList* command_line_arguments;
  static RogueString* executable_filepath;

  // PROPERTIES

};

struct RogueClassFile : RogueObject
{
  // PROPERTIES
  RogueString* filepath;

};

struct RogueClassProgram : RogueObject
{
  // PROPERTIES
  RogueString* code_prefix;
  RogueString* program_name;
  RogueInteger unique_integer;
  RogueTemplateList* template_list;
  RogueClassString_TemplateTable* template_lookup;
  RogueString* first_filepath;
  RogueClassToken* explicit_main_class_t;
  RogueString* explicit_main_class_name;
  RogueClassToken* implicit_main_class_t;
  RogueString* implicit_main_class_name;
  RogueClassType* main_class;
  RogueTypeList* type_list;
  RogueClassString_TypeTable* type_lookup;
  RogueClassType* type_null;
  RogueClassType* type_Real;
  RogueClassType* type_Float;
  RogueClassType* type_Long;
  RogueClassType* type_Integer;
  RogueClassType* type_Character;
  RogueClassType* type_Byte;
  RogueClassType* type_Logical;
  RogueClassType* type_Object;
  RogueClassType* type_String;
  RogueClassType* type_NativeArray;
  RogueClassType* type_GenericList;
  RogueClassType* type_Global;
  RogueClassType* type_Error;
  RogueClassType* type_StringBuilder;
  RogueClassType* type_FileReader;
  RogueClassType* type_FileWriter;
  RogueClassString_IntegerTable* literal_string_lookup;
  RogueStringList* literal_string_list;
  RogueStringBuilder* string_buffer;

};

struct RogueClassToken : RogueObject
{
  // PROPERTIES
  RogueClassTokenType* _type;
  RogueString* filepath;
  RogueInteger line;
  RogueInteger column;

};

struct RogueClassRogueError : RogueClassError
{
  // PROPERTIES
  RogueString* filepath;
  RogueInteger line;
  RogueInteger column;

};

struct RogueClassStringListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassParser : RogueObject
{
  // PROPERTIES
  RogueClassTokenReader* reader;
  RogueClassType* _this_type;
  RogueClassMethod* this_method;
  RogueLocalList* local_declarations;
  RoguePropertyList* property_list;
  RogueStringBuilder* string_buffer;
  RogueClassCmdStatementList* cur_statement_list;

};

struct RogueTokenList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueTemplateList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassTemplate : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueString* name;
  RogueTokenList* tokens;
  RogueClassAttributes* attributes;
  RogueTypeParameterList* type_parameters;

};

struct RogueClassString_TemplateTable : RogueObject
{
  // PROPERTIES
  RogueInteger bin_mask;
  RogueString_TemplateTableEntryList* bins;
  RogueStringList* keys;

};

struct RogueClassType : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueString* name;
  RogueClassAttributes* attributes;
  RogueInteger index;
  RogueLogical defined;
  RogueLogical organized;
  RogueLogical resolved;
  RogueLogical visiting;
  RogueClassType* base_class;
  RogueTypeList* base_types;
  RogueTypeList* flat_base_types;
  RogueLogical is_array;
  RogueLogical is_list;
  RogueLogical is_optional;
  RogueClassType* _element_type;
  RogueLogical simplify_name;
  RogueStringList* definition_list;
  RogueClassString_CmdTable* definition_lookup;
  RogueClassCmd* prev_enum_cmd;
  RogueInteger next_enum_offset;
  RoguePropertyList* settings_list;
  RogueClassString_PropertyTable* settings_lookup;
  RoguePropertyList* property_list;
  RogueClassString_PropertyTable* property_lookup;
  RogueMethodList* routine_list;
  RogueClassString_MethodListTable* routine_lookup_by_name;
  RogueClassString_MethodTable* routine_lookup_by_signature;
  RogueMethodList* method_list;
  RogueClassString_MethodListTable* method_lookup_by_name;
  RogueClassString_MethodTable* method_lookup_by_signature;
  RogueString* cpp_name;
  RogueString* cpp_class_name;
  RogueString* cpp_type_name;
  RogueInteger dynamic_method_table_index;

};

struct RogueTypeList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassString_TypeTable : RogueObject
{
  // PROPERTIES
  RogueInteger bin_mask;
  RogueString_TypeTableEntryList* bins;
  RogueStringList* keys;

};

struct RogueClassString_IntegerTable : RogueObject
{
  // PROPERTIES
  RogueInteger bin_mask;
  RogueString_IntegerTableEntryList* bins;
  RogueStringList* keys;

};

struct RogueClassAttribute : RogueObject
{
  // PROPERTIES

};

struct RogueClassAttributes : RogueObject
{
  // PROPERTIES
  RogueInteger flags;
  RogueStringList* tags;

};

struct RogueClassMethod : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassType* type_context;
  RogueString* name;
  RogueString* signature;
  RogueString* cpp_name;
  RogueString* cpp_typedef;
  RogueClassAttributes* attributes;
  RogueClassType* _return_type;
  RogueClassType* _task_result_type;
  RogueLocalList* parameters;
  RogueLocalList* locals;
  RogueInteger min_args;
  RogueClassCmdStatementList* statements;
  RogueClassCmdStatementList* aspect_statements;
  RogueTypeList* incorporating_classes;
  RogueClassMethod* overridden_method;
  RogueString* native_code;
  RogueLogical organized;
  RogueLogical resolved;
  RogueInteger index;

};

struct RogueClassCmd : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;

};

struct RogueClassCmdReturn : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* value;

};

struct RogueClassCmdStatement : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;

};

struct RogueClassCmdStatementList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueCmdList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassTokenType : RogueObject
{
  // SETTINGS
  static RogueClassString_TokenTypeTable* lookup;
  static RogueClassTokenType* directive_define;
  static RogueClassTokenType* directive_include;
  static RogueClassTokenType* directive_if;
  static RogueClassTokenType* directive_elseIf;
  static RogueClassTokenType* directive_else;
  static RogueClassTokenType* directive_endIf;
  static RogueClassTokenType* placeholder_id;
  static RogueClassTokenType* keyword_aspect;
  static RogueClassTokenType* keyword_case;
  static RogueClassTokenType* keyword_catch;
  static RogueClassTokenType* keyword_class;
  static RogueClassTokenType* keyword_compound;
  static RogueClassTokenType* keyword_DEFINITIONS;
  static RogueClassTokenType* keyword_else;
  static RogueClassTokenType* keyword_elseIf;
  static RogueClassTokenType* keyword_endAspect;
  static RogueClassTokenType* keyword_endClass;
  static RogueClassTokenType* keyword_endCompound;
  static RogueClassTokenType* keyword_endContingent;
  static RogueClassTokenType* keyword_endForEach;
  static RogueClassTokenType* keyword_endFunction;
  static RogueClassTokenType* keyword_endIf;
  static RogueClassTokenType* keyword_endLoop;
  static RogueClassTokenType* keyword_endPrimitive;
  static RogueClassTokenType* keyword_endTry;
  static RogueClassTokenType* keyword_endWhich;
  static RogueClassTokenType* keyword_endWhile;
  static RogueClassTokenType* keyword_ENUMERATE;
  static RogueClassTokenType* keyword_method;
  static RogueClassTokenType* keyword_METHODS;
  static RogueClassTokenType* keyword_others;
  static RogueClassTokenType* keyword_primitive;
  static RogueClassTokenType* keyword_PROPERTIES;
  static RogueClassTokenType* keyword_routine;
  static RogueClassTokenType* keyword_ROUTINES;
  static RogueClassTokenType* keyword_satisfied;
  static RogueClassTokenType* keyword_SETTINGS;
  static RogueClassTokenType* keyword_unsatisfied;
  static RogueClassTokenType* keyword_with;
  static RogueClassTokenType* symbol_close_brace;
  static RogueClassTokenType* symbol_close_bracket;
  static RogueClassTokenType* symbol_close_comment;
  static RogueClassTokenType* symbol_close_paren;
  static RogueClassTokenType* symbol_close_specialize;
  static RogueClassTokenType* eol;
  static RogueClassTokenType* keyword_await;
  static RogueClassTokenType* keyword_contingent;
  static RogueClassTokenType* keyword_escapeContingent;
  static RogueClassTokenType* keyword_escapeForEach;
  static RogueClassTokenType* keyword_escapeIf;
  static RogueClassTokenType* keyword_escapeLoop;
  static RogueClassTokenType* keyword_escapeTry;
  static RogueClassTokenType* keyword_escapeWhich;
  static RogueClassTokenType* keyword_escapeWhile;
  static RogueClassTokenType* keyword_forEach;
  static RogueClassTokenType* keyword_function;
  static RogueClassTokenType* keyword_if;
  static RogueClassTokenType* keyword_in;
  static RogueClassTokenType* keyword_inline;
  static RogueClassTokenType* keyword_is;
  static RogueClassTokenType* keyword_isNot;
  static RogueClassTokenType* keyword_local;
  static RogueClassTokenType* keyword_loop;
  static RogueClassTokenType* keyword_native;
  static RogueClassTokenType* keyword_necessary;
  static RogueClassTokenType* keyword_nextIteration;
  static RogueClassTokenType* keyword_noAction;
  static RogueClassTokenType* keyword_inlineNative;
  static RogueClassTokenType* keyword_null;
  static RogueClassTokenType* keyword_of;
  static RogueClassTokenType* keyword_return;
  static RogueClassTokenType* keyword_step;
  static RogueClassTokenType* keyword_sufficient;
  static RogueClassTokenType* keyword_throw;
  static RogueClassTokenType* keyword_trace;
  static RogueClassTokenType* keyword_try;
  static RogueClassTokenType* keyword_which;
  static RogueClassTokenType* keyword_while;
  static RogueClassTokenType* keyword_yield;
  static RogueClassTokenType* identifier;
  static RogueClassTokenType* type_identifier;
  static RogueClassTokenType* literal_character;
  static RogueClassTokenType* literal_integer;
  static RogueClassTokenType* literal_long;
  static RogueClassTokenType* literal_real;
  static RogueClassTokenType* literal_string;
  static RogueClassTokenType* keyword_and;
  static RogueClassTokenType* keyword_as;
  static RogueClassTokenType* keyword_false;
  static RogueClassTokenType* keyword_instanceOf;
  static RogueClassTokenType* keyword_meta;
  static RogueClassTokenType* keyword_not;
  static RogueClassTokenType* keyword_notInstanceOf;
  static RogueClassTokenType* keyword_or;
  static RogueClassTokenType* keyword_pi;
  static RogueClassTokenType* keyword_prior;
  static RogueClassTokenType* keyword_this;
  static RogueClassTokenType* keyword_true;
  static RogueClassTokenType* keyword_xor;
  static RogueClassTokenType* symbol_ampersand;
  static RogueClassTokenType* symbol_ampersand_equals;
  static RogueClassTokenType* symbol_arrow;
  static RogueClassTokenType* symbol_at;
  static RogueClassTokenType* symbol_backslash;
  static RogueClassTokenType* symbol_caret;
  static RogueClassTokenType* symbol_caret_equals;
  static RogueClassTokenType* symbol_colon;
  static RogueClassTokenType* symbol_colon_colon;
  static RogueClassTokenType* symbol_comma;
  static RogueClassTokenType* symbol_compare;
  static RogueClassTokenType* symbol_dot;
  static RogueClassTokenType* symbol_dot_equals;
  static RogueClassTokenType* symbol_downToGreaterThan;
  static RogueClassTokenType* symbol_empty_braces;
  static RogueClassTokenType* symbol_empty_brackets;
  static RogueClassTokenType* symbol_eq;
  static RogueClassTokenType* symbol_equals;
  static RogueClassTokenType* symbol_exclamation_point;
  static RogueClassTokenType* symbol_fat_arrow;
  static RogueClassTokenType* symbol_ge;
  static RogueClassTokenType* symbol_gt;
  static RogueClassTokenType* symbol_le;
  static RogueClassTokenType* symbol_lt;
  static RogueClassTokenType* symbol_minus;
  static RogueClassTokenType* symbol_minus_equals;
  static RogueClassTokenType* symbol_minus_minus;
  static RogueClassTokenType* symbol_ne;
  static RogueClassTokenType* symbol_open_brace;
  static RogueClassTokenType* symbol_open_bracket;
  static RogueClassTokenType* symbol_open_paren;
  static RogueClassTokenType* symbol_open_specialize;
  static RogueClassTokenType* symbol_percent;
  static RogueClassTokenType* symbol_percent_equals;
  static RogueClassTokenType* symbol_plus;
  static RogueClassTokenType* symbol_plus_equals;
  static RogueClassTokenType* symbol_plus_plus;
  static RogueClassTokenType* symbol_question_mark;
  static RogueClassTokenType* symbol_semicolon;
  static RogueClassTokenType* symbol_shift_left;
  static RogueClassTokenType* symbol_shift_right;
  static RogueClassTokenType* symbol_shift_right_x;
  static RogueClassTokenType* symbol_slash;
  static RogueClassTokenType* symbol_slash_equals;
  static RogueClassTokenType* symbol_tilde;
  static RogueClassTokenType* symbol_tilde_equals;
  static RogueClassTokenType* symbol_times;
  static RogueClassTokenType* symbol_times_equals;
  static RogueClassTokenType* symbol_upTo;
  static RogueClassTokenType* symbol_upToLessThan;
  static RogueClassTokenType* symbol_vertical_bar;
  static RogueClassTokenType* symbol_vertical_bar_equals;
  static RogueClassTokenType* symbol_double_vertical_bar;

  // PROPERTIES
  RogueString* name;

};

struct RogueMethodList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassCPPWriter : RogueObject
{
  // PROPERTIES
  RogueString* filepath;
  RogueStringBuilder* buffer;
  RogueInteger indent;
  RogueLogical needs_indent;
  RogueStringBuilder* temp_buffer;

};

struct RogueClassCharacterReader
{
  // PROPERTIES

};

struct RogueClassString_MethodTable : RogueObject
{
  // PROPERTIES
  RogueInteger bin_mask;
  RogueString_MethodTableEntryList* bins;
  RogueStringList* keys;

};

struct RogueLocalList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassLocal : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueString* name;
  RogueClassType* _type;
  RogueInteger index;
  RogueClassCmd* initial_value;
  RogueString* _cpp_name;

};

struct RogueClassTaskManager : RogueObject
{
  // PROPERTIES
  RogueTaskList* active_list;
  RogueTaskList* update_list;

};

struct RogueClassTask : RogueObject
{
  // PROPERTIES

};

struct RogueTaskList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassTaskManager__await_all__task127 : RogueObject
{
  // PROPERTIES
  RogueTaskList* tasks_0;
  RogueLogical still_waiting_1;
  RogueInteger i_2;
  RogueClassTask* task_3;
  RogueLogical active_4;
  RogueClassError* error_5;
  RogueClassTaskManager* context;
  RogueInteger ip;

};

struct RogueClassSystemEventQueue : RogueObject
{
  // PROPERTIES

};

struct RogueClassEventManager : RogueObject
{
  // PROPERTIES
  RogueInteger event_id_counter;

};

struct RogueClassRandom : RogueObject
{
  // PROPERTIES
  RogueLong state;

};

struct RogueClassCharacterListOps : RogueObject
{
  // PROPERTIES

};

struct RogueString_LogicalTableEntryList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassString_LogicalTableEntry : RogueObject
{
  // PROPERTIES
  RogueString* key;
  RogueLogical value;
  RogueClassString_LogicalTableEntry* next_entry;
  RogueInteger hash;

};

struct RogueByteList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassTokenReader : RogueObject
{
  // PROPERTIES
  RogueTokenList* tokens;
  RogueInteger position;
  RogueInteger count;

};

struct RoguePropertyList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassProperty : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassType* type_context;
  RogueString* name;
  RogueClassType* _type;
  RogueInteger attributes;
  RogueClassCmd* initial_value;
  RogueString* cpp_name;

};

struct RogueClassTokenizer : RogueObject
{
  // PROPERTIES
  RogueString* filepath;
  RogueClassParseReader* reader;
  RogueTokenList* tokens;
  RogueStringBuilder* buffer;
  RogueString* next_filepath;
  RogueInteger next_line;
  RogueInteger next_column;

};

struct RogueClassParseReader : RogueObject
{
  // PROPERTIES
  RogueCharacterList* data;
  RogueInteger position;
  RogueInteger count;
  RogueInteger line;
  RogueInteger column;

};

struct RogueClassPreprocessor : RogueObject
{
  // SETTINGS
  static RogueClassString_TokenListTable* definitions;

  // PROPERTIES
  RogueClassPreprocessorTokenReader* reader;
  RogueTokenList* tokens;

};

struct RogueClassString_TokenTypeTable : RogueObject
{
  // PROPERTIES
  RogueInteger bin_mask;
  RogueString_TokenTypeTableEntryList* bins;
  RogueStringList* keys;

};

struct RogueClassLiteralCharacterToken : RogueObject
{
  // PROPERTIES
  RogueClassTokenType* _type;
  RogueString* filepath;
  RogueInteger line;
  RogueInteger column;
  RogueCharacter value;

};

struct RogueClassLiteralIntegerToken : RogueObject
{
  // PROPERTIES
  RogueClassTokenType* _type;
  RogueString* filepath;
  RogueInteger line;
  RogueInteger column;
  RogueInteger value;

};

struct RogueClassLiteralRealToken : RogueObject
{
  // PROPERTIES
  RogueClassTokenType* _type;
  RogueString* filepath;
  RogueInteger line;
  RogueInteger column;
  RogueReal value;

};

struct RogueClassLiteralStringToken : RogueObject
{
  // PROPERTIES
  RogueClassTokenType* _type;
  RogueString* filepath;
  RogueInteger line;
  RogueInteger column;
  RogueString* value;

};

struct RogueClassTypeIdentifierToken : RogueObject
{
  // PROPERTIES
  RogueClassTokenType* _type;
  RogueString* filepath;
  RogueInteger line;
  RogueInteger column;
  RogueClassType* _nominal_type;
  RogueClassType* _target_type;

};

struct RogueClassTypeParameter : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueString* name;

};

struct RogueClassCmdLiteralInteger : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueInteger value;

};

struct RogueClassCmdLiteral : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;

};

struct RogueClassCloneArgs : RogueObject
{
  // PROPERTIES

};

struct RogueClassCmdArgs : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassCmdAdd : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;

};

struct RogueClassCmdBinary : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;

};

struct RogueClassString_CmdTable : RogueObject
{
  // PROPERTIES
  RogueInteger bin_mask;
  RogueString_CmdTableEntryList* bins;
  RogueStringList* keys;

};

struct RogueClassCmdIf : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmdStatementList* statements;
  RogueInteger _control_type;
  RogueLogical contains_yield;
  RogueString* escape_label;
  RogueString* upkeep_label;
  RogueClassCmdTaskControlSection* task_escape_section;
  RogueClassCmdTaskControlSection* task_upkeep_section;
  RogueClassCmdControlStructure* cloned_command;
  RogueClassCmd* condition;
  RogueClassCmdStatementList* else_statements;

};

struct RogueClassCmdControlStructure : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmdStatementList* statements;
  RogueInteger _control_type;
  RogueLogical contains_yield;
  RogueString* escape_label;
  RogueString* upkeep_label;
  RogueClassCmdTaskControlSection* task_escape_section;
  RogueClassCmdTaskControlSection* task_upkeep_section;
  RogueClassCmdControlStructure* cloned_command;

};

struct RogueClassCmdWhich : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmdStatementList* statements;
  RogueInteger _control_type;
  RogueLogical contains_yield;
  RogueString* escape_label;
  RogueString* upkeep_label;
  RogueClassCmdTaskControlSection* task_escape_section;
  RogueClassCmdTaskControlSection* task_upkeep_section;
  RogueClassCmdControlStructure* cloned_command;
  RogueClassCmd* expression;
  RogueCmdWhichCaseList* cases;
  RogueClassCmdWhichCase* case_others;

};

struct RogueClassCmdContingent : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmdStatementList* statements;
  RogueInteger _control_type;
  RogueLogical contains_yield;
  RogueString* escape_label;
  RogueString* upkeep_label;
  RogueClassCmdTaskControlSection* task_escape_section;
  RogueClassCmdTaskControlSection* task_upkeep_section;
  RogueClassCmdControlStructure* cloned_command;
  RogueClassCmdStatementList* satisfied_statements;
  RogueClassCmdStatementList* unsatisfied_statements;
  RogueString* satisfied_label;
  RogueString* unsatisfied_label;
  RogueClassCmdTaskControlSection* satisfied_section;
  RogueClassCmdTaskControlSection* unsatisfied_section;

};

struct RogueClassCmdGenericLoop : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmdStatementList* statements;
  RogueInteger _control_type;
  RogueLogical contains_yield;
  RogueString* escape_label;
  RogueString* upkeep_label;
  RogueClassCmdTaskControlSection* task_escape_section;
  RogueClassCmdTaskControlSection* task_upkeep_section;
  RogueClassCmdControlStructure* cloned_command;
  RogueClassCmdStatementList* control_statements;
  RogueClassCmd* condition;
  RogueClassCmdStatementList* upkeep;

};

struct RogueClassCmdTry : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmdStatementList* statements;
  RogueInteger _control_type;
  RogueLogical contains_yield;
  RogueString* escape_label;
  RogueString* upkeep_label;
  RogueClassCmdTaskControlSection* task_escape_section;
  RogueClassCmdTaskControlSection* task_upkeep_section;
  RogueClassCmdControlStructure* cloned_command;
  RogueCmdCatchList* catches;

};

struct RogueClassCmdAwait : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* expression;
  RogueClassCmdStatementList* statement_list;
  RogueClassLocal* result_var;

};

struct RogueClassCmdYield : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;

};

struct RogueClassCmdThrow : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* expression;

};

struct RogueClassCmdTrace : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueString* value;

};

struct RogueClassCmdEscape : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueInteger _control_type;
  RogueClassCmdControlStructure* target_cmd;

};

struct RogueClassCmdNextIteration : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmdControlStructure* target_cmd;

};

struct RogueClassCmdNecessary : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmdContingent* target_cmd;
  RogueClassCmd* condition;

};

struct RogueClassCmdSufficient : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmdContingent* target_cmd;
  RogueClassCmd* condition;

};

struct RogueClassCmdAdjust : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* operand;
  RogueInteger delta;

};

struct RogueClassCmdAssign : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* target;
  RogueClassCmd* new_value;

};

struct RogueClassCmdOpWithAssign : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* target;
  RogueClassTokenType* op;
  RogueClassCmd* new_value;

};

struct RogueClassCmdAccess : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueString* name;
  RogueClassCmdArgs* args;

};

struct RogueCmdWhichCaseList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassCmdWhichCase : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmdArgs* conditions;
  RogueClassCmdStatementList* statements;

};

struct RogueCmdCatchList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassCmdCatch : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassLocal* error_var;
  RogueClassCmdStatementList* statements;

};

struct RogueClassCmdLocalDeclaration : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassLocal* local_info;

};

struct RogueClassCmdAdjustLocal : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassLocal* local_info;
  RogueInteger delta;

};

struct RogueClassCmdReadLocal : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassLocal* local_info;

};

struct RogueClassCmdCompareLE : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;
  RogueLogical resolved;

};

struct RogueClassCmdComparison : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;
  RogueLogical resolved;

};

struct RogueClassCmdRange : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* first;
  RogueClassCmd* last;
  RogueClassCmd* step_size;

};

struct RogueClassCmdLocalOpWithAssign : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassTokenType* op;
  RogueClassCmd* new_value;
  RogueClassLocal* local_info;

};

struct RogueClassCmdResolvedOpWithAssign : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassTokenType* op;
  RogueClassCmd* new_value;

};

struct RogueClassCmdForEach : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmdStatementList* statements;
  RogueInteger _control_type;
  RogueLogical contains_yield;
  RogueString* escape_label;
  RogueString* upkeep_label;
  RogueClassCmdTaskControlSection* task_escape_section;
  RogueClassCmdTaskControlSection* task_upkeep_section;
  RogueClassCmdControlStructure* cloned_command;
  RogueString* control_var_name;
  RogueString* index_var_name;
  RogueClassCmd* collection;
  RogueClassCmd* step_cmd;

};

struct RogueClassCmdRangeUpTo : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* first;
  RogueClassCmd* last;
  RogueClassCmd* step_size;

};

struct RogueClassCmdLogicalXor : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;

};

struct RogueClassCmdBinaryLogical : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;

};

struct RogueClassCmdLogicalOr : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;

};

struct RogueClassCmdLogicalAnd : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;

};

struct RogueClassCmdCompareEQ : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;
  RogueLogical resolved;

};

struct RogueClassCmdCompareIs : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;
  RogueLogical resolved;

};

struct RogueClassCmdCompareNE : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;
  RogueLogical resolved;

};

struct RogueClassCmdCompareIsNot : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;
  RogueLogical resolved;

};

struct RogueClassCmdCompareLT : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;
  RogueLogical resolved;

};

struct RogueClassCmdCompareGT : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;
  RogueLogical resolved;

};

struct RogueClassCmdCompareGE : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;
  RogueLogical resolved;

};

struct RogueClassCmdInstanceOf : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* operand;
  RogueClassType* _target_type;

};

struct RogueClassCmdTypeOperator : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* operand;
  RogueClassType* _target_type;

};

struct RogueClassCmdLogicalNot : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* operand;

};

struct RogueClassCmdUnary : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* operand;

};

struct RogueClassCmdBitwiseXor : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;

};

struct RogueClassCmdBitwiseOp : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;

};

struct RogueClassCmdBitwiseOr : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;

};

struct RogueClassCmdBitwiseAnd : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;

};

struct RogueClassCmdBitwiseShiftLeft : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;

};

struct RogueClassCmdBitwiseShiftRight : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;

};

struct RogueClassCmdBitwiseShiftRightX : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;

};

struct RogueClassCmdSubtract : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;

};

struct RogueClassCmdMultiply : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;

};

struct RogueClassCmdDivide : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;

};

struct RogueClassCmdMod : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;

};

struct RogueClassCmdPower : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* left;
  RogueClassCmd* right;

};

struct RogueClassCmdNegate : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* operand;

};

struct RogueClassCmdBitwiseNot : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* operand;

};

struct RogueClassCmdLogicalize : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* operand;

};

struct RogueClassCmdGetOptionalValue : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* value;

};

struct RogueClassCmdElementAccess : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueClassCmd* index;

};

struct RogueClassCmdConvertToType : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* operand;
  RogueClassType* _target_type;

};

struct RogueClassCmdCreateCallback : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueString* name;
  RogueString* signature;
  RogueClassType* _return_type;

};

struct RogueClassCmdAs : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* operand;
  RogueClassType* _target_type;

};

struct RogueClassCmdDefaultValue : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassType* _of_type;

};

struct RogueClassCmdFormattedString : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueString* format;
  RogueClassCmdArgs* args;

};

struct RogueClassCmdLiteralString : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueString* value;
  RogueInteger index;

};

struct RogueClassCmdLiteralNull : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;

};

struct RogueClassCmdLiteralReal : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueReal value;

};

struct RogueClassCmdLiteralCharacter : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueCharacter value;

};

struct RogueClassCmdLiteralThis : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassType* _this_type;

};

struct RogueClassCmdThisContext : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassType* _this_type;

};

struct RogueClassCmdLiteralLogical : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueLogical value;

};

struct RogueClassCmdCreateList : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmdArgs* args;
  RogueClassType* _list_type;

};

struct RogueClassCmdCallPriorMethod : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueString* name;
  RogueClassCmdArgs* args;

};

struct RogueFnParamList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassFnParam : RogueObject
{
  // PROPERTIES
  RogueString* name;
  RogueClassType* _type;

};

struct RogueFnArgList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassFnArg : RogueObject
{
  // PROPERTIES
  RogueString* name;
  RogueClassCmd* value;
  RogueClassType* _type;

};

struct RogueClassCmdCreateFunction : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueFnParamList* parameters;
  RogueClassType* _return_type;
  RogueFnArgList* with_args;
  RogueClassCmdStatementList* statements;

};

struct RogueClassTokenListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassTemplateListOps : RogueObject
{
  // PROPERTIES

};

struct RogueTypeParameterList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassString_TypeSpecializerTable : RogueObject
{
  // PROPERTIES
  RogueInteger bin_mask;
  RogueString_TypeSpecializerTableEntryList* bins;
  RogueStringList* keys;

};

struct RogueClassTypeSpecializer : RogueObject
{
  // PROPERTIES
  RogueString* name;
  RogueInteger index;
  RogueTokenList* tokens;

};

struct RogueString_TemplateTableEntryList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassString_TemplateTableEntry : RogueObject
{
  // PROPERTIES
  RogueString* key;
  RogueClassTemplate* value;
  RogueClassString_TemplateTableEntry* next_entry;
  RogueInteger hash;

};

struct RogueClassString_PropertyTable : RogueObject
{
  // PROPERTIES
  RogueInteger bin_mask;
  RogueString_PropertyTableEntryList* bins;
  RogueStringList* keys;

};

struct RogueClassString_MethodListTable : RogueObject
{
  // PROPERTIES
  RogueInteger bin_mask;
  RogueString_MethodListTableEntryList* bins;
  RogueStringList* keys;

};

struct RogueClassCmdCreateCompound : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassType* _of_type;
  RogueClassCmdArgs* args;

};

struct RogueClassCmdWriteSetting : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassProperty* setting_info;
  RogueClassCmd* new_value;

};

struct RogueClassMethodListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassCmdWriteProperty : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueClassProperty* property_info;
  RogueClassCmd* new_value;

};

struct RogueClassTypeListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassPropertyListOps : RogueObject
{
  // PROPERTIES

};

struct RogueString_TypeTableEntryList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassString_TypeTableEntry : RogueObject
{
  // PROPERTIES
  RogueString* key;
  RogueClassType* value;
  RogueClassString_TypeTableEntry* next_entry;
  RogueInteger hash;

};

struct RogueString_IntegerTableEntryList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassString_IntegerTableEntry : RogueObject
{
  // PROPERTIES
  RogueString* key;
  RogueInteger value;
  RogueClassString_IntegerTableEntry* next_entry;
  RogueInteger hash;

};

struct RogueClassScope : RogueObject
{
  // PROPERTIES
  RogueClassType* _this_type;
  RogueClassMethod* this_method;
  RogueLocalList* local_list;
  RogueCmdControlStructureList* control_stack;

};

struct RogueCmdControlStructureList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassTaskArgs : RogueObject
{
  // PROPERTIES
  RogueClassType* _task_type;
  RogueClassMethod* task_method;
  RogueClassType* _original_type;
  RogueClassMethod* original_method;
  RogueClassCmdTaskControl* cmd_task_control;
  RogueClassProperty* context_property;
  RogueClassProperty* ip_property;

};

struct RogueClassCmdTaskControl : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueCmdTaskControlSectionList* sections;
  RogueClassCmdTaskControlSection* current_section;

};

struct RogueClassCmdTaskControlSection : RogueObject
{
  // PROPERTIES
  RogueInteger ip;
  RogueClassCmdStatementList* statements;

};

struct RogueClassCmdCastToType : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* operand;
  RogueClassType* _target_type;

};

struct RogueClassCmdCreateOptionalValue : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassType* _of_type;
  RogueClassCmd* value;

};

struct RogueClassCmdListOps : RogueObject
{
  // PROPERTIES

};

struct RogueString_MethodTableEntryList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassString_MethodTableEntry : RogueObject
{
  // PROPERTIES
  RogueString* key;
  RogueClassMethod* value;
  RogueClassString_MethodTableEntry* next_entry;
  RogueInteger hash;

};

struct RogueClassLocalListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassTaskListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassTime : RogueObject
{
  // PROPERTIES

};

struct RogueClassString_LogicalTableEntryListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassByteListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassDirectiveTokenType : RogueObject
{
  // PROPERTIES
  RogueString* name;

};

struct RogueClassEOLTokenType : RogueObject
{
  // PROPERTIES
  RogueString* name;

};

struct RogueClassStructureTokenType : RogueObject
{
  // PROPERTIES
  RogueString* name;

};

struct RogueClassOpWithAssignTokenType : RogueObject
{
  // PROPERTIES
  RogueString* name;

};

struct RogueClassEOLToken : RogueObject
{
  // PROPERTIES
  RogueClassTokenType* _type;
  RogueString* filepath;
  RogueInteger line;
  RogueInteger column;
  RogueString* comment;

};

struct RogueClassString_TokenListTable : RogueObject
{
  // PROPERTIES
  RogueInteger bin_mask;
  RogueString_TokenListTableEntryList* bins;
  RogueStringList* keys;

};

struct RogueClassPreprocessorTokenReader : RogueObject
{
  // PROPERTIES
  RogueTokenList* tokens;
  RogueTokenList* queue;
  RogueInteger position;
  RogueInteger count;

};

struct RogueString_TokenTypeTableEntryList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassString_TokenTypeTableEntry : RogueObject
{
  // PROPERTIES
  RogueString* key;
  RogueClassTokenType* value;
  RogueClassString_TokenTypeTableEntry* next_entry;
  RogueInteger hash;

};

struct RogueString_CmdTableEntryList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassString_CmdTableEntry : RogueObject
{
  // PROPERTIES
  RogueString* key;
  RogueClassCmd* value;
  RogueClassString_CmdTableEntry* next_entry;
  RogueInteger hash;

};

struct RogueCmdTaskControlSectionList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassCmdBlock : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmdStatementList* statements;
  RogueInteger _control_type;
  RogueLogical contains_yield;
  RogueString* escape_label;
  RogueString* upkeep_label;
  RogueClassCmdTaskControlSection* task_escape_section;
  RogueClassCmdTaskControlSection* task_upkeep_section;
  RogueClassCmdControlStructure* cloned_command;

};

struct RogueClassCmdReadProperty : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueClassProperty* property_info;

};

struct RogueClassCmdWriteLocal : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassLocal* local_info;
  RogueClassCmd* new_value;

};

struct RogueClassInlineArgs : RogueObject
{
  // PROPERTIES
  RogueClassCmd* this_context;
  RogueClassMethod* method_info;
  RogueClassString_CmdTable* arg_lookup;

};

struct RogueClassCmdReadSingleton : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassType* _of_type;

};

struct RogueClassCmdCreateArray : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassType* _array_type;
  RogueClassCmd* count_cmd;

};

struct RogueClassCmdCallRoutine : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueClassMethod* method_info;
  RogueClassCmdArgs* args;

};

struct RogueClassCmdCall : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueClassMethod* method_info;
  RogueClassCmdArgs* args;

};

struct RogueClassCmdCreateObject : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassType* _of_type;

};

struct RogueClassCmdReadSetting : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassProperty* setting_info;

};

struct RogueClassCmdLogicalizeOptionalValue : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* value;
  RogueLogical positive;

};

struct RogueClassCmdOpAssignSetting : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassTokenType* op;
  RogueClassCmd* new_value;
  RogueClassProperty* setting_info;

};

struct RogueClassCmdOpAssignProperty : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassTokenType* op;
  RogueClassCmd* new_value;
  RogueClassCmd* context;
  RogueClassProperty* property_info;

};

struct RogueClassCmdWhichCaseListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassCmdCatchListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassCmdReadArrayElement : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueClassType* _array_type;
  RogueClassCmd* index;

};

struct RogueClassCmdWriteArrayElement : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueClassType* _array_type;
  RogueClassCmd* index;
  RogueClassCmd* new_value;

};

struct RogueClassCmdConvertToPrimitiveType : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* operand;
  RogueClassType* _target_type;

};

struct RogueClassFnParamListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassFnArgListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassTypeParameterListOps : RogueObject
{
  // PROPERTIES

};

struct RogueString_TypeSpecializerTableEntryList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassString_TypeSpecializerTableEntry : RogueObject
{
  // PROPERTIES
  RogueString* key;
  RogueClassTypeSpecializer* value;
  RogueClassString_TypeSpecializerTableEntry* next_entry;
  RogueInteger hash;

};

struct RogueClassString_TemplateTableEntryListOps : RogueObject
{
  // PROPERTIES

};

struct RogueString_PropertyTableEntryList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassString_PropertyTableEntry : RogueObject
{
  // PROPERTIES
  RogueString* key;
  RogueClassProperty* value;
  RogueClassString_PropertyTableEntry* next_entry;
  RogueInteger hash;

};

struct RogueString_MethodListTableEntryList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassString_MethodListTableEntry : RogueObject
{
  // PROPERTIES
  RogueString* key;
  RogueMethodList* value;
  RogueClassString_MethodListTableEntry* next_entry;
  RogueInteger hash;

};

struct RogueClassString_TypeTableEntryListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassString_IntegerTableEntryListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassCmdCallInlineNativeRoutine : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueClassMethod* method_info;
  RogueClassCmdArgs* args;

};

struct RogueClassCmdCallInlineNative : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueClassMethod* method_info;
  RogueClassCmdArgs* args;

};

struct RogueClassCmdCallNativeRoutine : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueClassMethod* method_info;
  RogueClassCmdArgs* args;

};

struct RogueClassCmdReadArrayCount : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueClassType* _array_type;

};

struct RogueClassCmdCallInlineNativeMethod : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueClassMethod* method_info;
  RogueClassCmdArgs* args;

};

struct RogueClassCmdCallNativeMethod : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueClassMethod* method_info;
  RogueClassCmdArgs* args;

};

struct RogueClassCmdCallAspectMethod : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueClassMethod* method_info;
  RogueClassCmdArgs* args;

};

struct RogueClassCmdCallDynamicMethod : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueClassMethod* method_info;
  RogueClassCmdArgs* args;

};

struct RogueClassCmdCallMethod : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueClassMethod* method_info;
  RogueClassCmdArgs* args;

};

struct RogueClassCandidateMethods : RogueObject
{
  // PROPERTIES
  RogueClassType* type_context;
  RogueClassCmdAccess* access;
  RogueMethodList* available;
  RogueMethodList* compatible;
  RogueLogical error_on_fail;

};

struct RogueClassCmdControlStructureListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassString_MethodTableEntryListOps : RogueObject
{
  // PROPERTIES

};

struct RogueString_TokenListTableEntryList : RogueObject
{
  // PROPERTIES
  RogueArray* data;
  RogueInteger count;

};

struct RogueClassString_TokenListTableEntry : RogueObject
{
  // PROPERTIES
  RogueString* key;
  RogueTokenList* value;
  RogueClassString_TokenListTableEntry* next_entry;
  RogueInteger hash;

};

struct RogueClassString_TokenTypeTableEntryListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassString_CmdTableEntryListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassCmdTaskControlSectionListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassCmdAdjustProperty : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueClassProperty* property_info;
  RogueInteger delta;

};

struct RogueClassString_TypeSpecializerTableEntryListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassString_PropertyTableEntryListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassString_MethodListTableEntryListOps : RogueObject
{
  // PROPERTIES

};

struct RogueClassCmdCallStaticMethod : RogueObject
{
  // PROPERTIES
  RogueClassToken* t;
  RogueClassCmd* context;
  RogueClassMethod* method_info;
  RogueClassCmdArgs* args;

};

struct RogueClassString_TokenListTableEntryListOps : RogueObject
{
  // PROPERTIES

};


struct RogueProgram : RogueProgramCore
{
  RogueTypeCharacterList* type_CharacterList;
  RogueTypeGenericList* type_GenericList;
  RogueTypeStringBuilder* type_StringBuilder;
  RogueTypeStringList* type_StringList;
  RogueTypeGlobal* type_Global;
  RogueTypeConsole* type_Console;
  RogueTypeError* type_Error;
  RogueTypeRogueC* type_RogueC;
  RogueTypeString_LogicalTable* type_String_LogicalTable;
  RogueTypeSystem* type_System;
  RogueTypeFile* type_File;
  RogueTypeProgram* type_Program;
  RogueTypeToken* type_Token;
  RogueTypeRogueError* type_RogueError;
  RogueTypeStringListOps* type_StringListOps;
  RogueTypeParser* type_Parser;
  RogueTypeTokenList* type_TokenList;
  RogueTypeTemplateList* type_TemplateList;
  RogueTypeTemplate* type_Template;
  RogueTypeString_TemplateTable* type_String_TemplateTable;
  RogueTypeType* type_Type;
  RogueTypeTypeList* type_TypeList;
  RogueTypeString_TypeTable* type_String_TypeTable;
  RogueTypeString_IntegerTable* type_String_IntegerTable;
  RogueTypeAttribute* type_Attribute;
  RogueTypeAttributes* type_Attributes;
  RogueTypeMethod* type_Method;
  RogueTypeCmd* type_Cmd;
  RogueTypeCmdReturn* type_CmdReturn;
  RogueTypeCmdStatement* type_CmdStatement;
  RogueTypeCmdStatementList* type_CmdStatementList;
  RogueTypeCmdList* type_CmdList;
  RogueTypeTokenType* type_TokenType;
  RogueTypeMethodList* type_MethodList;
  RogueTypeCPPWriter* type_CPPWriter;
  RogueTypeCharacterReader* type_CharacterReader;
  RogueTypeString_MethodTable* type_String_MethodTable;
  RogueTypeLocalList* type_LocalList;
  RogueTypeLocal* type_Local;
  RogueTypeTaskManager* type_TaskManager;
  RogueTypeTask* type_Task;
  RogueTypeTaskList* type_TaskList;
  RogueTypeTaskManager__await_all__task127* type_TaskManager__await_all__task127;
  RogueTypeSystemEventQueue* type_SystemEventQueue;
  RogueTypeEventManager* type_EventManager;
  RogueTypeRandom* type_Random;
  RogueTypeCharacterListOps* type_CharacterListOps;
  RogueTypeString_LogicalTableEntryList* type_String_LogicalTableEntryList;
  RogueTypeString_LogicalTableEntry* type_String_LogicalTableEntry;
  RogueTypeByteList* type_ByteList;
  RogueTypeTokenReader* type_TokenReader;
  RogueTypePropertyList* type_PropertyList;
  RogueTypeProperty* type_Property;
  RogueTypeTokenizer* type_Tokenizer;
  RogueTypeParseReader* type_ParseReader;
  RogueTypePreprocessor* type_Preprocessor;
  RogueTypeString_TokenTypeTable* type_String_TokenTypeTable;
  RogueTypeLiteralCharacterToken* type_LiteralCharacterToken;
  RogueTypeLiteralIntegerToken* type_LiteralIntegerToken;
  RogueTypeLiteralRealToken* type_LiteralRealToken;
  RogueTypeLiteralStringToken* type_LiteralStringToken;
  RogueTypeTypeIdentifierToken* type_TypeIdentifierToken;
  RogueTypeTypeParameter* type_TypeParameter;
  RogueTypeCmdLiteralInteger* type_CmdLiteralInteger;
  RogueTypeCmdLiteral* type_CmdLiteral;
  RogueTypeCloneArgs* type_CloneArgs;
  RogueTypeCmdArgs* type_CmdArgs;
  RogueTypeCmdAdd* type_CmdAdd;
  RogueTypeCmdBinary* type_CmdBinary;
  RogueTypeString_CmdTable* type_String_CmdTable;
  RogueTypeCmdIf* type_CmdIf;
  RogueTypeCmdControlStructure* type_CmdControlStructure;
  RogueTypeCmdWhich* type_CmdWhich;
  RogueTypeCmdContingent* type_CmdContingent;
  RogueTypeCmdGenericLoop* type_CmdGenericLoop;
  RogueTypeCmdTry* type_CmdTry;
  RogueTypeCmdAwait* type_CmdAwait;
  RogueTypeCmdYield* type_CmdYield;
  RogueTypeCmdThrow* type_CmdThrow;
  RogueTypeCmdTrace* type_CmdTrace;
  RogueTypeCmdEscape* type_CmdEscape;
  RogueTypeCmdNextIteration* type_CmdNextIteration;
  RogueTypeCmdNecessary* type_CmdNecessary;
  RogueTypeCmdSufficient* type_CmdSufficient;
  RogueTypeCmdAdjust* type_CmdAdjust;
  RogueTypeCmdAssign* type_CmdAssign;
  RogueTypeCmdOpWithAssign* type_CmdOpWithAssign;
  RogueTypeCmdAccess* type_CmdAccess;
  RogueTypeCmdWhichCaseList* type_CmdWhichCaseList;
  RogueTypeCmdWhichCase* type_CmdWhichCase;
  RogueTypeCmdCatchList* type_CmdCatchList;
  RogueTypeCmdCatch* type_CmdCatch;
  RogueTypeCmdLocalDeclaration* type_CmdLocalDeclaration;
  RogueTypeCmdAdjustLocal* type_CmdAdjustLocal;
  RogueTypeCmdReadLocal* type_CmdReadLocal;
  RogueTypeCmdCompareLE* type_CmdCompareLE;
  RogueTypeCmdComparison* type_CmdComparison;
  RogueTypeCmdRange* type_CmdRange;
  RogueTypeCmdLocalOpWithAssign* type_CmdLocalOpWithAssign;
  RogueTypeCmdResolvedOpWithAssign* type_CmdResolvedOpWithAssign;
  RogueTypeCmdForEach* type_CmdForEach;
  RogueTypeCmdRangeUpTo* type_CmdRangeUpTo;
  RogueTypeCmdLogicalXor* type_CmdLogicalXor;
  RogueTypeCmdBinaryLogical* type_CmdBinaryLogical;
  RogueTypeCmdLogicalOr* type_CmdLogicalOr;
  RogueTypeCmdLogicalAnd* type_CmdLogicalAnd;
  RogueTypeCmdCompareEQ* type_CmdCompareEQ;
  RogueTypeCmdCompareIs* type_CmdCompareIs;
  RogueTypeCmdCompareNE* type_CmdCompareNE;
  RogueTypeCmdCompareIsNot* type_CmdCompareIsNot;
  RogueTypeCmdCompareLT* type_CmdCompareLT;
  RogueTypeCmdCompareGT* type_CmdCompareGT;
  RogueTypeCmdCompareGE* type_CmdCompareGE;
  RogueTypeCmdInstanceOf* type_CmdInstanceOf;
  RogueTypeCmdTypeOperator* type_CmdTypeOperator;
  RogueTypeCmdLogicalNot* type_CmdLogicalNot;
  RogueTypeCmdUnary* type_CmdUnary;
  RogueTypeCmdBitwiseXor* type_CmdBitwiseXor;
  RogueTypeCmdBitwiseOp* type_CmdBitwiseOp;
  RogueTypeCmdBitwiseOr* type_CmdBitwiseOr;
  RogueTypeCmdBitwiseAnd* type_CmdBitwiseAnd;
  RogueTypeCmdBitwiseShiftLeft* type_CmdBitwiseShiftLeft;
  RogueTypeCmdBitwiseShiftRight* type_CmdBitwiseShiftRight;
  RogueTypeCmdBitwiseShiftRightX* type_CmdBitwiseShiftRightX;
  RogueTypeCmdSubtract* type_CmdSubtract;
  RogueTypeCmdMultiply* type_CmdMultiply;
  RogueTypeCmdDivide* type_CmdDivide;
  RogueTypeCmdMod* type_CmdMod;
  RogueTypeCmdPower* type_CmdPower;
  RogueTypeCmdNegate* type_CmdNegate;
  RogueTypeCmdBitwiseNot* type_CmdBitwiseNot;
  RogueTypeCmdLogicalize* type_CmdLogicalize;
  RogueTypeCmdGetOptionalValue* type_CmdGetOptionalValue;
  RogueTypeCmdElementAccess* type_CmdElementAccess;
  RogueTypeCmdConvertToType* type_CmdConvertToType;
  RogueTypeCmdCreateCallback* type_CmdCreateCallback;
  RogueTypeCmdAs* type_CmdAs;
  RogueTypeCmdDefaultValue* type_CmdDefaultValue;
  RogueTypeCmdFormattedString* type_CmdFormattedString;
  RogueTypeCmdLiteralString* type_CmdLiteralString;
  RogueTypeCmdLiteralNull* type_CmdLiteralNull;
  RogueTypeCmdLiteralReal* type_CmdLiteralReal;
  RogueTypeCmdLiteralCharacter* type_CmdLiteralCharacter;
  RogueTypeCmdLiteralThis* type_CmdLiteralThis;
  RogueTypeCmdThisContext* type_CmdThisContext;
  RogueTypeCmdLiteralLogical* type_CmdLiteralLogical;
  RogueTypeCmdCreateList* type_CmdCreateList;
  RogueTypeCmdCallPriorMethod* type_CmdCallPriorMethod;
  RogueTypeFnParamList* type_FnParamList;
  RogueTypeFnParam* type_FnParam;
  RogueTypeFnArgList* type_FnArgList;
  RogueTypeFnArg* type_FnArg;
  RogueTypeCmdCreateFunction* type_CmdCreateFunction;
  RogueTypeTokenListOps* type_TokenListOps;
  RogueTypeTemplateListOps* type_TemplateListOps;
  RogueTypeTypeParameterList* type_TypeParameterList;
  RogueTypeString_TypeSpecializerTable* type_String_TypeSpecializerTable;
  RogueTypeTypeSpecializer* type_TypeSpecializer;
  RogueTypeString_TemplateTableEntryList* type_String_TemplateTableEntryList;
  RogueTypeString_TemplateTableEntry* type_String_TemplateTableEntry;
  RogueTypeString_PropertyTable* type_String_PropertyTable;
  RogueTypeString_MethodListTable* type_String_MethodListTable;
  RogueTypeCmdCreateCompound* type_CmdCreateCompound;
  RogueTypeCmdWriteSetting* type_CmdWriteSetting;
  RogueTypeMethodListOps* type_MethodListOps;
  RogueTypeCmdWriteProperty* type_CmdWriteProperty;
  RogueTypeTypeListOps* type_TypeListOps;
  RogueTypePropertyListOps* type_PropertyListOps;
  RogueTypeString_TypeTableEntryList* type_String_TypeTableEntryList;
  RogueTypeString_TypeTableEntry* type_String_TypeTableEntry;
  RogueTypeString_IntegerTableEntryList* type_String_IntegerTableEntryList;
  RogueTypeString_IntegerTableEntry* type_String_IntegerTableEntry;
  RogueTypeScope* type_Scope;
  RogueTypeCmdControlStructureList* type_CmdControlStructureList;
  RogueTypeTaskArgs* type_TaskArgs;
  RogueTypeCmdTaskControl* type_CmdTaskControl;
  RogueTypeCmdTaskControlSection* type_CmdTaskControlSection;
  RogueTypeCmdCastToType* type_CmdCastToType;
  RogueTypeCmdCreateOptionalValue* type_CmdCreateOptionalValue;
  RogueTypeCmdListOps* type_CmdListOps;
  RogueTypeString_MethodTableEntryList* type_String_MethodTableEntryList;
  RogueTypeString_MethodTableEntry* type_String_MethodTableEntry;
  RogueTypeLocalListOps* type_LocalListOps;
  RogueTypeTaskListOps* type_TaskListOps;
  RogueTypeTime* type_Time;
  RogueTypeString_LogicalTableEntryListOps* type_String_LogicalTableEntryListOps;
  RogueTypeByteListOps* type_ByteListOps;
  RogueTypeDirectiveTokenType* type_DirectiveTokenType;
  RogueTypeEOLTokenType* type_EOLTokenType;
  RogueTypeStructureTokenType* type_StructureTokenType;
  RogueTypeOpWithAssignTokenType* type_OpWithAssignTokenType;
  RogueTypeEOLToken* type_EOLToken;
  RogueTypeString_TokenListTable* type_String_TokenListTable;
  RogueTypePreprocessorTokenReader* type_PreprocessorTokenReader;
  RogueTypeString_TokenTypeTableEntryList* type_String_TokenTypeTableEntryList;
  RogueTypeString_TokenTypeTableEntry* type_String_TokenTypeTableEntry;
  RogueTypeString_CmdTableEntryList* type_String_CmdTableEntryList;
  RogueTypeString_CmdTableEntry* type_String_CmdTableEntry;
  RogueTypeCmdTaskControlSectionList* type_CmdTaskControlSectionList;
  RogueTypeCmdBlock* type_CmdBlock;
  RogueTypeCmdReadProperty* type_CmdReadProperty;
  RogueTypeCmdWriteLocal* type_CmdWriteLocal;
  RogueTypeInlineArgs* type_InlineArgs;
  RogueTypeCmdReadSingleton* type_CmdReadSingleton;
  RogueTypeCmdCreateArray* type_CmdCreateArray;
  RogueTypeCmdCallRoutine* type_CmdCallRoutine;
  RogueTypeCmdCall* type_CmdCall;
  RogueTypeCmdCreateObject* type_CmdCreateObject;
  RogueTypeCmdReadSetting* type_CmdReadSetting;
  RogueTypeCmdLogicalizeOptionalValue* type_CmdLogicalizeOptionalValue;
  RogueTypeCmdOpAssignSetting* type_CmdOpAssignSetting;
  RogueTypeCmdOpAssignProperty* type_CmdOpAssignProperty;
  RogueTypeCmdWhichCaseListOps* type_CmdWhichCaseListOps;
  RogueTypeCmdCatchListOps* type_CmdCatchListOps;
  RogueTypeCmdReadArrayElement* type_CmdReadArrayElement;
  RogueTypeCmdWriteArrayElement* type_CmdWriteArrayElement;
  RogueTypeCmdConvertToPrimitiveType* type_CmdConvertToPrimitiveType;
  RogueTypeFnParamListOps* type_FnParamListOps;
  RogueTypeFnArgListOps* type_FnArgListOps;
  RogueTypeTypeParameterListOps* type_TypeParameterListOps;
  RogueTypeString_TypeSpecializerTableEntryList* type_String_TypeSpecializerTableEntryList;
  RogueTypeString_TypeSpecializerTableEntry* type_String_TypeSpecializerTableEntry;
  RogueTypeString_TemplateTableEntryListOps* type_String_TemplateTableEntryListOps;
  RogueTypeString_PropertyTableEntryList* type_String_PropertyTableEntryList;
  RogueTypeString_PropertyTableEntry* type_String_PropertyTableEntry;
  RogueTypeString_MethodListTableEntryList* type_String_MethodListTableEntryList;
  RogueTypeString_MethodListTableEntry* type_String_MethodListTableEntry;
  RogueTypeString_TypeTableEntryListOps* type_String_TypeTableEntryListOps;
  RogueTypeString_IntegerTableEntryListOps* type_String_IntegerTableEntryListOps;
  RogueTypeCmdCallInlineNativeRoutine* type_CmdCallInlineNativeRoutine;
  RogueTypeCmdCallInlineNative* type_CmdCallInlineNative;
  RogueTypeCmdCallNativeRoutine* type_CmdCallNativeRoutine;
  RogueTypeCmdReadArrayCount* type_CmdReadArrayCount;
  RogueTypeCmdCallInlineNativeMethod* type_CmdCallInlineNativeMethod;
  RogueTypeCmdCallNativeMethod* type_CmdCallNativeMethod;
  RogueTypeCmdCallAspectMethod* type_CmdCallAspectMethod;
  RogueTypeCmdCallDynamicMethod* type_CmdCallDynamicMethod;
  RogueTypeCmdCallMethod* type_CmdCallMethod;
  RogueTypeCandidateMethods* type_CandidateMethods;
  RogueTypeCmdControlStructureListOps* type_CmdControlStructureListOps;
  RogueTypeString_MethodTableEntryListOps* type_String_MethodTableEntryListOps;
  RogueTypeString_TokenListTableEntryList* type_String_TokenListTableEntryList;
  RogueTypeString_TokenListTableEntry* type_String_TokenListTableEntry;
  RogueTypeString_TokenTypeTableEntryListOps* type_String_TokenTypeTableEntryListOps;
  RogueTypeString_CmdTableEntryListOps* type_String_CmdTableEntryListOps;
  RogueTypeCmdTaskControlSectionListOps* type_CmdTaskControlSectionListOps;
  RogueTypeCmdAdjustProperty* type_CmdAdjustProperty;
  RogueTypeString_TypeSpecializerTableEntryListOps* type_String_TypeSpecializerTableEntryListOps;
  RogueTypeString_PropertyTableEntryListOps* type_String_PropertyTableEntryListOps;
  RogueTypeString_MethodListTableEntryListOps* type_String_MethodListTableEntryListOps;
  RogueTypeCmdCallStaticMethod* type_CmdCallStaticMethod;
  RogueTypeString_TokenListTableEntryListOps* type_String_TokenListTableEntryListOps;

  RogueProgram();
  ~RogueProgram();
  void configure();
  void launch( int argc, char* argv[] );
  void finish_tasks();
};

RogueString* RogueReal__to_String( RogueReal THIS );
RogueString* RogueReal__type_name( RogueReal THIS );
RogueInteger RogueInteger__hash_code( RogueInteger THIS );
RogueString* RogueInteger__to_String( RogueInteger THIS );
RogueString* RogueInteger__type_name( RogueInteger THIS );
RogueString* RogueString__after_any( RogueString* THIS, RogueCharacter ch_0 );
RogueString* RogueString__after_any( RogueString* THIS, RogueString* st_0 );
RogueString* RogueString__after_first( RogueString* THIS, RogueCharacter ch_0 );
RogueString* RogueString__after_first( RogueString* THIS, RogueString* st_0 );
RogueString* RogueString__after_last( RogueString* THIS, RogueCharacter ch_0 );
RogueString* RogueString__after_last( RogueString* THIS, RogueString* st_0 );
RogueString* RogueString__before_first( RogueString* THIS, RogueCharacter ch_0 );
RogueString* RogueString__before_first( RogueString* THIS, RogueString* st_0 );
RogueString* RogueString__before_last( RogueString* THIS, RogueCharacter ch_0 );
RogueString* RogueString__before_last( RogueString* THIS, RogueString* st_0 );
RogueLogical RogueString__begins_with( RogueString* THIS, RogueCharacter ch_0 );
RogueLogical RogueString__begins_with( RogueString* THIS, RogueString* other_0 );
RogueLogical RogueString__contains( RogueString* THIS, RogueString* substring_0 );
RogueLogical RogueString__ends_with( RogueString* THIS, RogueString* other_0 );
RogueString* RogueString__from_first( RogueString* THIS, RogueCharacter ch_0 );
RogueString* RogueString__from_first( RogueString* THIS, RogueString* st_0 );
RogueString* RogueString__from_last( RogueString* THIS, RogueCharacter ch_0 );
RogueString* RogueString__from_last( RogueString* THIS, RogueString* st_0 );
RogueCharacter RogueString__last( RogueString* THIS );
RogueString* RogueString__left_justified( RogueString* THIS, RogueInteger spaces_0 );
RogueString* RogueString__leftmost( RogueString* THIS, RogueInteger n_0 );
RogueString* RogueString__operatorPLUS( RogueString* THIS, RogueLogical value_0 );
RogueString* RogueString__operatorPLUS( RogueString* THIS, RogueObject* value_0 );
RogueString* RogueString__reversed( RogueString* THIS );
RogueString* RogueString__right_justified( RogueString* THIS, RogueInteger spaces_0 );
RogueString* RogueString__rightmost( RogueString* THIS, RogueInteger n_0 );
RogueStringList* RogueString__split2( RogueString* THIS, RogueCharacter separator_0 );
RogueStringList* RogueString__split2( RogueString* THIS, RogueString* separator_0 );
RogueString* RogueString__to_String( RogueString* THIS );
RogueString* RogueString__to_lowercase( RogueString* THIS );
RogueString* RogueString__to_uppercase( RogueString* THIS );
RogueString* RogueString__type_name( RogueString* THIS );
RogueString* RogueCharacterList__to_String( RogueCharacterList* THIS );
RogueString* RogueCharacterList__type_name( RogueCharacterList* THIS );
RogueCharacterList* RogueCharacterList__init_object( RogueCharacterList* THIS );
RogueCharacterList* RogueCharacterList__init( RogueCharacterList* THIS );
RogueCharacterList* RogueCharacterList__init( RogueCharacterList* THIS, RogueInteger initial_capacity_0 );
RogueCharacterList* RogueCharacterList__init( RogueCharacterList* THIS, RogueInteger initial_capacity_0, RogueCharacter initial_value_1 );
RogueCharacterList* RogueCharacterList__clone( RogueCharacterList* THIS );
RogueCharacterList* RogueCharacterList__add( RogueCharacterList* THIS, RogueCharacter value_0 );
RogueCharacterList* RogueCharacterList__add( RogueCharacterList* THIS, RogueCharacterList* other_0 );
RogueInteger RogueCharacterList__capacity( RogueCharacterList* THIS );
RogueCharacterList* RogueCharacterList__clear( RogueCharacterList* THIS );
RogueCharacterList* RogueCharacterList__insert( RogueCharacterList* THIS, RogueCharacter value_0, RogueInteger before_index_1 );
RogueCharacter RogueCharacterList__last( RogueCharacterList* THIS );
RogueCharacter RogueCharacterList__random( RogueCharacterList* THIS );
RogueCharacterList* RogueCharacterList__reserve( RogueCharacterList* THIS, RogueInteger additional_count_0 );
RogueCharacter RogueCharacterList__remove( RogueCharacterList* THIS, RogueCharacter value_0 );
RogueCharacter RogueCharacterList__remove_at( RogueCharacterList* THIS, RogueInteger index_0 );
RogueCharacter RogueCharacterList__remove_last( RogueCharacterList* THIS );
RogueInteger RogueCharacter__hash_code( RogueCharacter THIS );
RogueLogical RogueCharacter__is_alphanumeric( RogueCharacter THIS );
RogueLogical RogueCharacter__is_letter( RogueCharacter THIS );
RogueString* RogueCharacter__to_String( RogueCharacter THIS );
RogueInteger RogueCharacter__to_number( RogueCharacter THIS );
RogueString* RogueCharacter__type_name( RogueCharacter THIS );
RogueString* RogueGenericList__type_name( RogueClassGenericList* THIS );
RogueClassGenericList* RogueGenericList__init_object( RogueClassGenericList* THIS );
RogueLogical RogueObject__operatorEQUALSEQUALS( RogueObject* THIS, RogueObject* other_0 );
RogueString* RogueObject__to_String( RogueObject* THIS );
RogueString* RogueObject__type_name( RogueObject* THIS );
RogueString* RogueStringBuilder__to_String( RogueStringBuilder* THIS );
RogueString* RogueStringBuilder__type_name( RogueStringBuilder* THIS );
RogueStringBuilder* RogueStringBuilder__init( RogueStringBuilder* THIS );
RogueStringBuilder* RogueStringBuilder__init( RogueStringBuilder* THIS, RogueInteger initial_capacity_0 );
RogueStringBuilder* RogueStringBuilder__add_indent( RogueStringBuilder* THIS, RogueInteger spaces_0 );
RogueStringBuilder* RogueStringBuilder__clear( RogueStringBuilder* THIS );
RogueLogical RogueStringBuilder__needs_indent( RogueStringBuilder* THIS );
RogueStringBuilder* RogueStringBuilder__print( RogueStringBuilder* THIS, RogueByte value_0 );
RogueStringBuilder* RogueStringBuilder__print( RogueStringBuilder* THIS, RogueCharacter value_0 );
RogueStringBuilder* RogueStringBuilder__print( RogueStringBuilder* THIS, RogueLogical value_0 );
RogueStringBuilder* RogueStringBuilder__print( RogueStringBuilder* THIS, RogueObject* value_0 );
RogueStringBuilder* RogueStringBuilder__print( RogueStringBuilder* THIS, RogueString* value_0 );
void RogueStringBuilder__print_indent( RogueStringBuilder* THIS );
RogueStringBuilder* RogueStringBuilder__println( RogueStringBuilder* THIS );
RogueStringBuilder* RogueStringBuilder__println( RogueStringBuilder* THIS, RogueByte value_0 );
RogueStringBuilder* RogueStringBuilder__println( RogueStringBuilder* THIS, RogueCharacter value_0 );
RogueStringBuilder* RogueStringBuilder__println( RogueStringBuilder* THIS, RogueInteger value_0 );
RogueStringBuilder* RogueStringBuilder__println( RogueStringBuilder* THIS, RogueLogical value_0 );
RogueStringBuilder* RogueStringBuilder__println( RogueStringBuilder* THIS, RogueLong value_0 );
RogueStringBuilder* RogueStringBuilder__println( RogueStringBuilder* THIS, RogueReal value_0, RogueInteger decimal_places_1 );
RogueStringBuilder* RogueStringBuilder__println( RogueStringBuilder* THIS, RogueObject* value_0 );
RogueStringBuilder* RogueStringBuilder__println( RogueStringBuilder* THIS, RogueString* value_0 );
RogueStringBuilder* RogueStringBuilder__reserve( RogueStringBuilder* THIS, RogueInteger additional_count_0 );
RogueStringBuilder* RogueStringBuilder__init_object( RogueStringBuilder* THIS );
RogueString* RogueOptionalInteger__type_name( RogueOptionalInteger THIS );
RogueInteger RogueLogical__hash_code( RogueLogical THIS );
RogueString* RogueLogical__to_String( RogueLogical THIS );
RogueString* RogueLogical__type_name( RogueLogical THIS );
RogueInteger RogueByte__hash_code( RogueByte THIS );
RogueString* RogueByte__to_String( RogueByte THIS );
RogueString* RogueByte__type_name( RogueByte THIS );
RogueString* RogueLong__type_name( RogueLong THIS );
RogueString* RogueFloat__type_name( RogueFloat THIS );
RogueString* RogueStringList__to_String( RogueStringList* THIS );
RogueString* RogueStringList__type_name( RogueStringList* THIS );
RogueStringList* RogueStringList__init_object( RogueStringList* THIS );
RogueStringList* RogueStringList__init( RogueStringList* THIS );
RogueStringList* RogueStringList__init( RogueStringList* THIS, RogueInteger initial_capacity_0 );
RogueStringList* RogueStringList__init( RogueStringList* THIS, RogueInteger initial_capacity_0, RogueString* initial_value_1 );
RogueStringList* RogueStringList__clone( RogueStringList* THIS );
RogueStringList* RogueStringList__add( RogueStringList* THIS, RogueString* value_0 );
RogueStringList* RogueStringList__add( RogueStringList* THIS, RogueStringList* other_0 );
RogueInteger RogueStringList__capacity( RogueStringList* THIS );
RogueStringList* RogueStringList__clear( RogueStringList* THIS );
RogueStringList* RogueStringList__insert( RogueStringList* THIS, RogueString* value_0, RogueInteger before_index_1 );
RogueString* RogueStringList__last( RogueStringList* THIS );
RogueString* RogueStringList__random( RogueStringList* THIS );
RogueStringList* RogueStringList__reserve( RogueStringList* THIS, RogueInteger additional_count_0 );
RogueString* RogueStringList__remove( RogueStringList* THIS, RogueString* value_0 );
RogueString* RogueStringList__remove_at( RogueStringList* THIS, RogueInteger index_0 );
RogueString* RogueStringList__remove_last( RogueStringList* THIS );
RogueString* RogueGlobal__type_name( RogueClassGlobal* THIS );
RogueClassGlobal* RogueGlobal__add_indent( RogueClassGlobal* THIS, RogueInteger spaces_0 );
RogueClassGlobal* RogueGlobal__flush( RogueClassGlobal* THIS );
RogueClassGlobal* RogueGlobal__print( RogueClassGlobal* THIS, RogueByte value_0 );
RogueClassGlobal* RogueGlobal__print( RogueClassGlobal* THIS, RogueCharacter value_0 );
RogueClassGlobal* RogueGlobal__print( RogueClassGlobal* THIS, RogueInteger value_0 );
RogueClassGlobal* RogueGlobal__print( RogueClassGlobal* THIS, RogueLogical value_0 );
RogueClassGlobal* RogueGlobal__print( RogueClassGlobal* THIS, RogueLong value_0 );
RogueClassGlobal* RogueGlobal__print( RogueClassGlobal* THIS, RogueObject* value_0 );
RogueClassGlobal* RogueGlobal__print( RogueClassGlobal* THIS, RogueReal value_0 );
RogueClassGlobal* RogueGlobal__print( RogueClassGlobal* THIS, RogueString* value_0 );
RogueClassGlobal* RogueGlobal__println( RogueClassGlobal* THIS );
RogueClassGlobal* RogueGlobal__println( RogueClassGlobal* THIS, RogueByte value_0 );
RogueClassGlobal* RogueGlobal__println( RogueClassGlobal* THIS, RogueCharacter value_0 );
RogueClassGlobal* RogueGlobal__println( RogueClassGlobal* THIS, RogueInteger value_0 );
RogueClassGlobal* RogueGlobal__println( RogueClassGlobal* THIS, RogueLogical value_0 );
RogueClassGlobal* RogueGlobal__println( RogueClassGlobal* THIS, RogueLong value_0 );
RogueClassGlobal* RogueGlobal__println( RogueClassGlobal* THIS, RogueReal value_0 );
RogueClassGlobal* RogueGlobal__println( RogueClassGlobal* THIS, RogueObject* value_0 );
RogueClassGlobal* RogueGlobal__println( RogueClassGlobal* THIS, RogueString* value_0 );
RogueClassGlobal* RogueGlobal__init_object( RogueClassGlobal* THIS );
RogueString* RogueConsole__type_name( RogueClassConsole* THIS );
void RogueConsole__print( RogueClassConsole* THIS, RogueString* value_0 );
void RogueConsole__print( RogueClassConsole* THIS, RogueStringBuilder* value_0 );
RogueClassConsole* RogueConsole__init_object( RogueClassConsole* THIS );
RogueString* RogueError__to_String( RogueClassError* THIS );
RogueString* RogueError__type_name( RogueClassError* THIS );
RogueClassError* RogueError__init( RogueClassError* THIS, RogueString* _auto_19_0 );
RogueClassError* RogueError__init_object( RogueClassError* THIS );
RogueString* RogueNativeArray__type_name( RogueArray* THIS );
RogueString* RogueRogueC__type_name( RogueClassRogueC* THIS );
RogueClassRogueC* RogueRogueC__init( RogueClassRogueC* THIS );
void RogueRogueC__include( RogueClassRogueC* THIS, RogueString* filepath_0 );
void RogueRogueC__include( RogueClassRogueC* THIS, RogueClassToken* t_0, RogueString* filepath_1 );
void RogueRogueC__process_command_line_arguments( RogueClassRogueC* THIS );
void RogueRogueC__require_valueless( RogueClassRogueC* THIS, RogueString* arg_0, RogueString* expecting_1 );
RogueClassRogueC* RogueRogueC__init_object( RogueClassRogueC* THIS );
RogueString* RogueString_LogicalTable__to_String( RogueClassString_LogicalTable* THIS );
RogueString* RogueString_LogicalTable__type_name( RogueClassString_LogicalTable* THIS );
RogueClassString_LogicalTable* RogueString_LogicalTable__init( RogueClassString_LogicalTable* THIS );
RogueClassString_LogicalTable* RogueString_LogicalTable__init( RogueClassString_LogicalTable* THIS, RogueInteger bin_count_0 );
RogueClassString_LogicalTable* RogueString_LogicalTable__init( RogueClassString_LogicalTable* THIS, RogueClassString_LogicalTable* other_0 );
RogueClassString_LogicalTable* RogueString_LogicalTable__add( RogueClassString_LogicalTable* THIS, RogueClassString_LogicalTable* other_0 );
RogueLogical RogueString_LogicalTable__at( RogueClassString_LogicalTable* THIS, RogueInteger index_0 );
void RogueString_LogicalTable__clear( RogueClassString_LogicalTable* THIS );
RogueClassString_LogicalTable* RogueString_LogicalTable__clone( RogueClassString_LogicalTable* THIS );
RogueLogical RogueString_LogicalTable__contains( RogueClassString_LogicalTable* THIS, RogueString* key_0 );
RogueInteger RogueString_LogicalTable__count( RogueClassString_LogicalTable* THIS );
RogueClassString_LogicalTableEntry* RogueString_LogicalTable__find( RogueClassString_LogicalTable* THIS, RogueString* key_0 );
RogueLogical RogueString_LogicalTable__get( RogueClassString_LogicalTable* THIS, RogueString* key_0 );
RogueLogical RogueString_LogicalTable__remove( RogueClassString_LogicalTable* THIS, RogueString* key_0 );
void RogueString_LogicalTable__set( RogueClassString_LogicalTable* THIS, RogueString* key_0, RogueLogical value_1 );
RogueStringBuilder* RogueString_LogicalTable__print_to( RogueClassString_LogicalTable* THIS, RogueStringBuilder* buffer_0 );
RogueClassString_LogicalTable* RogueString_LogicalTable__init_object( RogueClassString_LogicalTable* THIS );
RogueString* RogueSystem__type_name( RogueClassSystem* THIS );
RogueClassSystem* RogueSystem__init_object( RogueClassSystem* THIS );
RogueString* RogueFile__to_String( RogueClassFile* THIS );
RogueString* RogueFile__type_name( RogueClassFile* THIS );
RogueClassFile* RogueFile__init( RogueClassFile* THIS, RogueString* _auto_22_0 );
RogueString* RogueFile__filename( RogueClassFile* THIS );
RogueClassFile* RogueFile__init_object( RogueClassFile* THIS );
RogueString* RogueProgram__type_name( RogueClassProgram* THIS );
void RogueProgram__configure( RogueClassProgram* THIS );
RogueString* RogueProgram__create_unique_id( RogueClassProgram* THIS );
RogueInteger RogueProgram__next_unique_integer( RogueClassProgram* THIS );
RogueClassTemplate* RogueProgram__find_template( RogueClassProgram* THIS, RogueString* name_0 );
RogueClassType* Rogue_Program__find_type( RogueClassProgram* THIS, RogueString* name_0 );
RogueClassType* RogueProgram__get_type_reference( RogueClassProgram* THIS, RogueClassToken* t_0, RogueString* name_1 );
RogueString* RogueProgram__get_callback_type_signature( RogueClassProgram* THIS, RogueTypeList* parameter_types_0 );
RogueClassType* RogueProgram__get_callback_type_reference( RogueClassProgram* THIS, RogueClassToken* t_0, RogueTypeList* parameter_types_1, RogueClassType* return_type_2 );
RogueClassType* Rogue_Program__create_built_in_type( RogueClassProgram* THIS, RogueString* name_0, RogueInteger attributes_1 );
void RogueProgram__resolve( RogueClassProgram* THIS );
RogueString* RogueProgram__validate_cpp_name( RogueClassProgram* THIS, RogueString* name_0 );
void RogueProgram__write_cpp( RogueClassProgram* THIS, RogueString* filepath_0 );
RogueClassProgram* RogueProgram__init_object( RogueClassProgram* THIS );
RogueString* RogueStringArray__type_name( RogueArray* THIS );
RogueString* RogueToken__to_String( RogueClassToken* THIS );
RogueString* RogueToken__type_name( RogueClassToken* THIS );
RogueClassToken* RogueToken__init( RogueClassToken* THIS, RogueClassTokenType* _auto_26_0 );
RogueClassToken* RogueToken__clone( RogueClassToken* THIS );
RogueClassRogueError* RogueToken__error( RogueClassToken* THIS, RogueString* message_0 );
RogueLogical RogueToken__is_directive( RogueClassToken* THIS );
RogueLogical RogueToken__is_structure( RogueClassToken* THIS );
RogueString* RogueToken__quoted_name( RogueClassToken* THIS );
RogueClassToken* RogueToken__set_location( RogueClassToken* THIS, RogueString* _auto_27_0, RogueInteger _auto_28_1, RogueInteger _auto_29_2 );
RogueClassToken* RogueToken__set_location( RogueClassToken* THIS, RogueClassToken* existing_0 );
RogueCharacter RogueToken__to_Character( RogueClassToken* THIS );
RogueInteger RogueToken__to_Integer( RogueClassToken* THIS );
RogueReal RogueToken__to_Real( RogueClassToken* THIS );
RogueClassType* RogueToken__to_Type( RogueClassToken* THIS );
RogueClassToken* RogueToken__init_object( RogueClassToken* THIS );
RogueString* RogueRogueError__to_String( RogueClassRogueError* THIS );
RogueString* RogueRogueError__type_name( RogueClassRogueError* THIS );
RogueClassRogueError* RogueRogueError__init_object( RogueClassRogueError* THIS );
RogueClassRogueError* RogueRogueError__init( RogueClassRogueError* THIS, RogueString* _auto_33_0, RogueString* _auto_34_1, RogueInteger _auto_35_2, RogueInteger _auto_36_3 );
RogueString* RogueStringListOps__type_name( RogueClassStringListOps* THIS );
RogueClassStringListOps* RogueStringListOps__init_object( RogueClassStringListOps* THIS );
RogueString* RogueParser__type_name( RogueClassParser* THIS );
RogueClassParser* RogueParser__init( RogueClassParser* THIS, RogueString* filepath_0 );
RogueClassParser* RogueParser__init( RogueClassParser* THIS, RogueClassToken* t_0, RogueString* filepath_1, RogueString* data_2 );
RogueClassParser* RogueParser__init( RogueClassParser* THIS, RogueTokenList* tokens_0 );
RogueLogical RogueParser__consume( RogueClassParser* THIS, RogueClassTokenType* type_0 );
RogueLogical RogueParser__consume( RogueClassParser* THIS, RogueString* identifier_0 );
RogueLogical RogueParser__consume_end_commands( RogueClassParser* THIS );
RogueLogical RogueParser__consume_eols( RogueClassParser* THIS );
RogueClassRogueError* RogueParser__error( RogueClassParser* THIS, RogueString* message_0 );
void RogueParser__must_consume( RogueClassParser* THIS, RogueClassTokenType* type_0, RogueString* error_message_1 );
void RogueParser__must_consume_eols( RogueClassParser* THIS );
RogueClassToken* RogueParser__must_read( RogueClassParser* THIS, RogueClassTokenType* type_0 );
RogueLogical RogueParser__next_is( RogueClassParser* THIS, RogueClassTokenType* type_0 );
RogueLogical RogueParser__next_is_end_command( RogueClassParser* THIS );
RogueLogical RogueParser__next_is_statement( RogueClassParser* THIS );
void RogueParser__parse_elements( RogueClassParser* THIS );
RogueLogical RogueParser__parse_element( RogueClassParser* THIS );
void RogueParser__parse_class_template( RogueClassParser* THIS );
void RogueParser__parse_aspect_template( RogueClassParser* THIS );
void RogueParser__parse_compound_template( RogueClassParser* THIS );
void RogueParser__parse_primitive_template( RogueClassParser* THIS );
void RogueParser__parse_template_tokens( RogueClassParser* THIS, RogueClassTemplate* template_0, RogueClassTokenType* end_type_1 );
void RogueParser__parse_attributes( RogueClassParser* THIS, RogueClassAttributes* attributes_0 );
void Rogue_Parser__ensure_unspecialized_element_type( RogueClassParser* THIS, RogueClassToken* t_0, RogueClassAttributes* attributes_1 );
void RogueParser__parse_type_def( RogueClassParser* THIS, RogueClassType* _auto_37_0 );
RogueLogical RogueParser__parse_section( RogueClassParser* THIS );
RogueLogical RogueParser__parse_definitions( RogueClassParser* THIS, RogueLogical enumerate_0 );
RogueLogical RogueParser__parse_properties( RogueClassParser* THIS, RogueLogical as_settings_0 );
RogueLogical RogueParser__parse_method( RogueClassParser* THIS, RogueLogical as_routine_0 );
void RogueParser__parse_single_or_multi_line_statements( RogueClassParser* THIS, RogueClassCmdStatementList* statements_0, RogueClassTokenType* end_type_1 );
void RogueParser__parse_multi_line_statements( RogueClassParser* THIS, RogueClassCmdStatementList* statements_0 );
void RogueParser__parse_single_line_statements( RogueClassParser* THIS, RogueClassCmdStatementList* statements_0 );
void RogueParser__parse_statement( RogueClassParser* THIS, RogueClassCmdStatementList* statements_0, RogueLogical allow_control_structures_1 );
RogueClassCmdWhich* RogueParser__parse_which( RogueClassParser* THIS );
RogueClassCmdContingent* RogueParser__parse_contingent( RogueClassParser* THIS );
RogueClassCmdTry* RogueParser__parse_try( RogueClassParser* THIS );
void RogueParser__parse_local_declaration( RogueClassParser* THIS, RogueClassCmdStatementList* statements_0 );
RogueClassType* Rogue_Parser__parse_type( RogueClassParser* THIS );
RogueString* Rogue_Parser__parse_possible_type( RogueClassParser* THIS );
RogueClassCmdIf* RogueParser__parse_if( RogueClassParser* THIS );
RogueClassCmdGenericLoop* RogueParser__parse_loop( RogueClassParser* THIS );
RogueClassCmdGenericLoop* RogueParser__parse_while( RogueClassParser* THIS );
RogueClassCmd* RogueParser__parse_for_each( RogueClassParser* THIS );
RogueClassToken* RogueParser__peek( RogueClassParser* THIS );
RogueClassToken* RogueParser__read( RogueClassParser* THIS );
RogueString* RogueParser__read_identifier( RogueClassParser* THIS, RogueLogical allow_at_sign_0 );
RogueClassCmd* RogueParser__parse_expression( RogueClassParser* THIS );
RogueClassCmd* RogueParser__parse_range( RogueClassParser* THIS );
RogueClassCmd* RogueParser__parse_range( RogueClassParser* THIS, RogueClassCmd* lhs_0 );
RogueClassCmd* RogueParser__parse_logical_xor( RogueClassParser* THIS );
RogueClassCmd* RogueParser__parse_logical_xor( RogueClassParser* THIS, RogueClassCmd* lhs_0 );
RogueClassCmd* RogueParser__parse_logical_or( RogueClassParser* THIS );
RogueClassCmd* RogueParser__parse_logical_or( RogueClassParser* THIS, RogueClassCmd* lhs_0 );
RogueClassCmd* RogueParser__parse_logical_and( RogueClassParser* THIS );
RogueClassCmd* RogueParser__parse_logical_and( RogueClassParser* THIS, RogueClassCmd* lhs_0 );
RogueClassCmd* RogueParser__parse_comparison( RogueClassParser* THIS );
RogueClassCmd* RogueParser__parse_comparison( RogueClassParser* THIS, RogueClassCmd* lhs_0 );
RogueClassCmd* RogueParser__parse_bitwise_xor( RogueClassParser* THIS );
RogueClassCmd* RogueParser__parse_bitwise_xor( RogueClassParser* THIS, RogueClassCmd* lhs_0 );
RogueClassCmd* RogueParser__parse_bitwise_or( RogueClassParser* THIS );
RogueClassCmd* RogueParser__parse_bitwise_or( RogueClassParser* THIS, RogueClassCmd* lhs_0 );
RogueClassCmd* RogueParser__parse_bitwise_and( RogueClassParser* THIS );
RogueClassCmd* RogueParser__parse_bitwise_and( RogueClassParser* THIS, RogueClassCmd* lhs_0 );
RogueClassCmd* RogueParser__parse_bitwise_shift( RogueClassParser* THIS );
RogueClassCmd* RogueParser__parse_bitwise_shift( RogueClassParser* THIS, RogueClassCmd* lhs_0 );
RogueClassCmd* RogueParser__parse_add_subtract( RogueClassParser* THIS );
RogueClassCmd* RogueParser__parse_add_subtract( RogueClassParser* THIS, RogueClassCmd* lhs_0 );
RogueClassCmd* RogueParser__parse_multiply_divide_mod( RogueClassParser* THIS );
RogueClassCmd* RogueParser__parse_multiply_divide_mod( RogueClassParser* THIS, RogueClassCmd* lhs_0 );
RogueClassCmd* RogueParser__parse_power( RogueClassParser* THIS );
RogueClassCmd* RogueParser__parse_power( RogueClassParser* THIS, RogueClassCmd* lhs_0 );
RogueClassCmd* RogueParser__parse_pre_unary( RogueClassParser* THIS );
RogueClassCmd* RogueParser__parse_post_unary( RogueClassParser* THIS );
RogueClassCmd* RogueParser__parse_post_unary( RogueClassParser* THIS, RogueClassCmd* operand_0 );
RogueClassCmd* RogueParser__parse_member_access( RogueClassParser* THIS );
RogueClassCmd* RogueParser__parse_member_access( RogueClassParser* THIS, RogueClassCmd* context_0 );
RogueClassCmd* RogueParser__parse_access( RogueClassParser* THIS, RogueClassToken* t_0, RogueClassCmd* context_1 );
RogueClassCmdArgs* RogueParser__parse_args( RogueClassParser* THIS, RogueClassTokenType* start_type_0, RogueClassTokenType* end_type_1 );
RogueString* RogueParser__parse_specialization_string( RogueClassParser* THIS );
void RogueParser__parse_specializer( RogueClassParser* THIS, RogueStringBuilder* buffer_0, RogueTokenList* tokens_1 );
RogueClassCmd* RogueParser__parse_term( RogueClassParser* THIS );
RogueClassParser* RogueParser__init_object( RogueClassParser* THIS );
RogueString* RogueTokenList__to_String( RogueTokenList* THIS );
RogueString* RogueTokenList__type_name( RogueTokenList* THIS );
RogueTokenList* RogueTokenList__init_object( RogueTokenList* THIS );
RogueTokenList* RogueTokenList__init( RogueTokenList* THIS );
RogueTokenList* RogueTokenList__init( RogueTokenList* THIS, RogueInteger initial_capacity_0 );
RogueTokenList* RogueTokenList__init( RogueTokenList* THIS, RogueInteger initial_capacity_0, RogueClassToken* initial_value_1 );
RogueTokenList* RogueTokenList__clone( RogueTokenList* THIS );
RogueTokenList* RogueTokenList__add( RogueTokenList* THIS, RogueClassToken* value_0 );
RogueTokenList* RogueTokenList__add( RogueTokenList* THIS, RogueTokenList* other_0 );
RogueInteger RogueTokenList__capacity( RogueTokenList* THIS );
RogueTokenList* RogueTokenList__clear( RogueTokenList* THIS );
RogueTokenList* RogueTokenList__insert( RogueTokenList* THIS, RogueClassToken* value_0, RogueInteger before_index_1 );
RogueClassToken* RogueTokenList__last( RogueTokenList* THIS );
RogueClassToken* RogueTokenList__random( RogueTokenList* THIS );
RogueTokenList* RogueTokenList__reserve( RogueTokenList* THIS, RogueInteger additional_count_0 );
RogueClassToken* RogueTokenList__remove( RogueTokenList* THIS, RogueClassToken* value_0 );
RogueClassToken* RogueTokenList__remove_at( RogueTokenList* THIS, RogueInteger index_0 );
RogueClassToken* RogueTokenList__remove_last( RogueTokenList* THIS );
RogueString* RogueTemplateList__to_String( RogueTemplateList* THIS );
RogueString* RogueTemplateList__type_name( RogueTemplateList* THIS );
RogueTemplateList* RogueTemplateList__init_object( RogueTemplateList* THIS );
RogueTemplateList* RogueTemplateList__init( RogueTemplateList* THIS );
RogueTemplateList* RogueTemplateList__init( RogueTemplateList* THIS, RogueInteger initial_capacity_0 );
RogueTemplateList* RogueTemplateList__init( RogueTemplateList* THIS, RogueInteger initial_capacity_0, RogueClassTemplate* initial_value_1 );
RogueTemplateList* RogueTemplateList__clone( RogueTemplateList* THIS );
RogueTemplateList* RogueTemplateList__add( RogueTemplateList* THIS, RogueClassTemplate* value_0 );
RogueTemplateList* RogueTemplateList__add( RogueTemplateList* THIS, RogueTemplateList* other_0 );
RogueInteger RogueTemplateList__capacity( RogueTemplateList* THIS );
RogueTemplateList* RogueTemplateList__clear( RogueTemplateList* THIS );
RogueTemplateList* RogueTemplateList__insert( RogueTemplateList* THIS, RogueClassTemplate* value_0, RogueInteger before_index_1 );
RogueClassTemplate* RogueTemplateList__last( RogueTemplateList* THIS );
RogueClassTemplate* RogueTemplateList__random( RogueTemplateList* THIS );
RogueTemplateList* RogueTemplateList__reserve( RogueTemplateList* THIS, RogueInteger additional_count_0 );
RogueClassTemplate* RogueTemplateList__remove( RogueTemplateList* THIS, RogueClassTemplate* value_0 );
RogueClassTemplate* RogueTemplateList__remove_at( RogueTemplateList* THIS, RogueInteger index_0 );
RogueClassTemplate* RogueTemplateList__remove_last( RogueTemplateList* THIS );
RogueString* RogueTemplate__type_name( RogueClassTemplate* THIS );
RogueClassTemplate* RogueTemplate__init( RogueClassTemplate* THIS, RogueClassToken* _auto_41_0, RogueString* _auto_42_1, RogueInteger attribute_flags_2 );
RogueClassTypeParameter* RogueTemplate__add_type_parameter( RogueClassTemplate* THIS, RogueClassToken* p_t_0, RogueString* p_name_1 );
RogueInteger Rogue_Template__element_type( RogueClassTemplate* THIS );
void RogueTemplate__instantiate( RogueClassTemplate* THIS, RogueClassType* type_0 );
void RogueTemplate__instantiate_list( RogueClassTemplate* THIS, RogueClassType* type_0 );
void RogueTemplate__instantiate_optional( RogueClassTemplate* THIS, RogueClassType* type_0 );
void Rogue_Template__instantiate_parameterized_type( RogueClassTemplate* THIS, RogueClassType* type_0 );
void Rogue_Template__instantiate_standard_type( RogueClassTemplate* THIS, RogueClassType* type_0 );
RogueClassTemplate* RogueTemplate__init_object( RogueClassTemplate* THIS );
RogueString* RogueString_TemplateTable__to_String( RogueClassString_TemplateTable* THIS );
RogueString* RogueString_TemplateTable__type_name( RogueClassString_TemplateTable* THIS );
RogueClassString_TemplateTable* RogueString_TemplateTable__init( RogueClassString_TemplateTable* THIS );
RogueClassString_TemplateTable* RogueString_TemplateTable__init( RogueClassString_TemplateTable* THIS, RogueInteger bin_count_0 );
RogueClassString_TemplateTable* RogueString_TemplateTable__init( RogueClassString_TemplateTable* THIS, RogueClassString_TemplateTable* other_0 );
RogueClassString_TemplateTable* RogueString_TemplateTable__add( RogueClassString_TemplateTable* THIS, RogueClassString_TemplateTable* other_0 );
RogueClassTemplate* RogueString_TemplateTable__at( RogueClassString_TemplateTable* THIS, RogueInteger index_0 );
void RogueString_TemplateTable__clear( RogueClassString_TemplateTable* THIS );
RogueClassString_TemplateTable* RogueString_TemplateTable__clone( RogueClassString_TemplateTable* THIS );
RogueLogical RogueString_TemplateTable__contains( RogueClassString_TemplateTable* THIS, RogueString* key_0 );
RogueInteger RogueString_TemplateTable__count( RogueClassString_TemplateTable* THIS );
RogueClassString_TemplateTableEntry* RogueString_TemplateTable__find( RogueClassString_TemplateTable* THIS, RogueString* key_0 );
RogueClassTemplate* RogueString_TemplateTable__get( RogueClassString_TemplateTable* THIS, RogueString* key_0 );
RogueClassTemplate* RogueString_TemplateTable__remove( RogueClassString_TemplateTable* THIS, RogueString* key_0 );
void RogueString_TemplateTable__set( RogueClassString_TemplateTable* THIS, RogueString* key_0, RogueClassTemplate* value_1 );
RogueStringBuilder* RogueString_TemplateTable__print_to( RogueClassString_TemplateTable* THIS, RogueStringBuilder* buffer_0 );
RogueClassString_TemplateTable* RogueString_TemplateTable__init_object( RogueClassString_TemplateTable* THIS );
RogueString* RogueType__to_String( RogueClassType* THIS );
RogueString* RogueType__type_name( RogueClassType* THIS );
RogueClassType* RogueType__init( RogueClassType* THIS, RogueClassToken* _auto_44_0, RogueString* _auto_45_1 );
RogueClassMethod* RogueType__add_method( RogueClassType* THIS, RogueClassToken* m_t_0, RogueString* m_name_1 );
RogueClassMethod* RogueType__add_routine( RogueClassType* THIS, RogueClassToken* m_t_0, RogueString* m_name_1 );
RogueClassProperty* RogueType__add_setting( RogueClassType* THIS, RogueClassToken* s_t_0, RogueString* s_name_1 );
RogueClassProperty* RogueType__add_property( RogueClassType* THIS, RogueClassToken* p_t_0, RogueString* p_name_1, RogueClassType* p_type_2, RogueClassCmd* initial_value_3 );
void RogueType__assign_cpp_name( RogueClassType* THIS );
RogueClassType* Rogue_Type__compile_type( RogueClassType* THIS );
RogueClassCmd* RogueType__create_default_value( RogueClassType* THIS, RogueClassToken* _t_0 );
void RogueType__declare_settings( RogueClassType* THIS, RogueClassCPPWriter* writer_0 );
RogueLogical RogueType__extends_object( RogueClassType* THIS );
RogueClassMethod* RogueType__find_method( RogueClassType* THIS, RogueString* signature_0 );
RogueClassMethod* RogueType__find_routine( RogueClassType* THIS, RogueString* signature_0 );
RogueClassProperty* RogueType__find_property( RogueClassType* THIS, RogueString* p_name_0 );
RogueClassProperty* RogueType__find_setting( RogueClassType* THIS, RogueString* s_name_0 );
RogueLogical RogueType__has_method_named( RogueClassType* THIS, RogueString* m_name_0 );
RogueLogical RogueType__has_routine_named( RogueClassType* THIS, RogueString* r_name_0 );
RogueLogical RogueType__instance_of( RogueClassType* THIS, RogueClassType* ancestor_type_0 );
RogueLogical RogueType__is_compatible_with( RogueClassType* THIS, RogueClassType* other_0 );
RogueLogical RogueType__is_direct( RogueClassType* THIS );
RogueLogical RogueType__is_aspect( RogueClassType* THIS );
RogueLogical RogueType__is_class( RogueClassType* THIS );
RogueLogical RogueType__is_compound( RogueClassType* THIS );
RogueLogical RogueType__is_functional( RogueClassType* THIS );
RogueLogical RogueType__is_immutable( RogueClassType* THIS );
RogueLogical RogueType__is_native( RogueClassType* THIS );
RogueLogical RogueType__is_primitive( RogueClassType* THIS );
RogueLogical RogueType__is_reference( RogueClassType* THIS );
RogueLogical RogueType__is_singleton( RogueClassType* THIS );
RogueClassType* RogueType__organize( RogueClassType* THIS );
void RogueType__collect_base_types( RogueClassType* THIS, RogueTypeList* list_0 );
RogueLogical RogueType__has_references( RogueClassType* THIS );
void RogueType__inherit_definitions( RogueClassType* THIS, RogueClassType* from_type_0 );
void RogueType__inherit_properties( RogueClassType* THIS, RoguePropertyList* list_0, RogueClassString_PropertyTable* lookup_1 );
void RogueType__inherit_property( RogueClassType* THIS, RogueClassProperty* p_0, RoguePropertyList* list_1, RogueClassString_PropertyTable* lookup_2 );
void RogueType__inherit_methods( RogueClassType* THIS, RogueMethodList* list_0, RogueClassString_MethodTable* lookup_1 );
void RogueType__inherit_method( RogueClassType* THIS, RogueClassMethod* m_0, RogueMethodList* list_1, RogueClassString_MethodTable* lookup_2 );
void RogueType__inherit_routines( RogueClassType* THIS, RogueMethodList* list_0, RogueClassString_MethodTable* lookup_1 );
void RogueType__inherit_routine( RogueClassType* THIS, RogueClassMethod* m_0, RogueMethodList* list_1, RogueClassString_MethodTable* lookup_2 );
void RogueType__index_and_move_inline_to_end( RogueClassType* THIS, RogueMethodList* list_0 );
RogueLogical RogueType__omit_output( RogueClassType* THIS );
RogueClassType* RogueType__resolve( RogueClassType* THIS );
void RogueType__print_data_definition( RogueClassType* THIS, RogueClassCPPWriter* writer_0 );
void RogueType__print_type_definition( RogueClassType* THIS, RogueClassCPPWriter* writer_0 );
void RogueType__print_routine_prototypes( RogueClassType* THIS, RogueClassCPPWriter* writer_0 );
void RogueType__print_routine_definitions( RogueClassType* THIS, RogueClassCPPWriter* writer_0 );
void RogueType__print_method_prototypes( RogueClassType* THIS, RogueClassCPPWriter* writer_0 );
void RogueType__determine_cpp_method_typedefs( RogueClassType* THIS, RogueStringList* list_0, RogueClassString_MethodTable* lookup_1 );
RogueInteger RogueType__print_dynamic_method_table_entries( RogueClassType* THIS, RogueInteger at_index_0, RogueClassCPPWriter* writer_1 );
void RogueType__print_method_definitions( RogueClassType* THIS, RogueClassCPPWriter* writer_0 );
void RogueType__print_type_configuration( RogueClassType* THIS, RogueClassCPPWriter* writer_0 );
RogueClassType* RogueType__init_object( RogueClassType* THIS );
RogueString* RogueTypeList__to_String( RogueTypeList* THIS );
RogueString* RogueTypeList__type_name( RogueTypeList* THIS );
RogueTypeList* RogueTypeList__init_object( RogueTypeList* THIS );
RogueTypeList* RogueTypeList__init( RogueTypeList* THIS );
RogueTypeList* RogueTypeList__init( RogueTypeList* THIS, RogueInteger initial_capacity_0 );
RogueTypeList* RogueTypeList__init( RogueTypeList* THIS, RogueInteger initial_capacity_0, RogueClassType* initial_value_1 );
RogueTypeList* RogueTypeList__clone( RogueTypeList* THIS );
RogueTypeList* RogueTypeList__add( RogueTypeList* THIS, RogueClassType* value_0 );
RogueTypeList* RogueTypeList__add( RogueTypeList* THIS, RogueTypeList* other_0 );
RogueInteger RogueTypeList__capacity( RogueTypeList* THIS );
RogueTypeList* RogueTypeList__clear( RogueTypeList* THIS );
RogueTypeList* RogueTypeList__insert( RogueTypeList* THIS, RogueClassType* value_0, RogueInteger before_index_1 );
RogueClassType* RogueTypeList__last( RogueTypeList* THIS );
RogueClassType* RogueTypeList__random( RogueTypeList* THIS );
RogueTypeList* RogueTypeList__reserve( RogueTypeList* THIS, RogueInteger additional_count_0 );
RogueClassType* RogueTypeList__remove( RogueTypeList* THIS, RogueClassType* value_0 );
RogueClassType* RogueTypeList__remove_at( RogueTypeList* THIS, RogueInteger index_0 );
RogueClassType* RogueTypeList__remove_last( RogueTypeList* THIS );
RogueString* RogueString_TypeTable__to_String( RogueClassString_TypeTable* THIS );
RogueString* RogueString_TypeTable__type_name( RogueClassString_TypeTable* THIS );
RogueClassString_TypeTable* RogueString_TypeTable__init( RogueClassString_TypeTable* THIS );
RogueClassString_TypeTable* RogueString_TypeTable__init( RogueClassString_TypeTable* THIS, RogueInteger bin_count_0 );
RogueClassString_TypeTable* RogueString_TypeTable__init( RogueClassString_TypeTable* THIS, RogueClassString_TypeTable* other_0 );
RogueClassString_TypeTable* RogueString_TypeTable__add( RogueClassString_TypeTable* THIS, RogueClassString_TypeTable* other_0 );
RogueClassType* RogueString_TypeTable__at( RogueClassString_TypeTable* THIS, RogueInteger index_0 );
void RogueString_TypeTable__clear( RogueClassString_TypeTable* THIS );
RogueClassString_TypeTable* RogueString_TypeTable__clone( RogueClassString_TypeTable* THIS );
RogueLogical RogueString_TypeTable__contains( RogueClassString_TypeTable* THIS, RogueString* key_0 );
RogueInteger RogueString_TypeTable__count( RogueClassString_TypeTable* THIS );
RogueClassString_TypeTableEntry* RogueString_TypeTable__find( RogueClassString_TypeTable* THIS, RogueString* key_0 );
RogueClassType* RogueString_TypeTable__get( RogueClassString_TypeTable* THIS, RogueString* key_0 );
RogueClassType* RogueString_TypeTable__remove( RogueClassString_TypeTable* THIS, RogueString* key_0 );
void RogueString_TypeTable__set( RogueClassString_TypeTable* THIS, RogueString* key_0, RogueClassType* value_1 );
RogueStringBuilder* RogueString_TypeTable__print_to( RogueClassString_TypeTable* THIS, RogueStringBuilder* buffer_0 );
RogueClassString_TypeTable* RogueString_TypeTable__init_object( RogueClassString_TypeTable* THIS );
RogueString* RogueString_IntegerTable__to_String( RogueClassString_IntegerTable* THIS );
RogueString* RogueString_IntegerTable__type_name( RogueClassString_IntegerTable* THIS );
RogueClassString_IntegerTable* RogueString_IntegerTable__init( RogueClassString_IntegerTable* THIS );
RogueClassString_IntegerTable* RogueString_IntegerTable__init( RogueClassString_IntegerTable* THIS, RogueInteger bin_count_0 );
RogueClassString_IntegerTable* RogueString_IntegerTable__init( RogueClassString_IntegerTable* THIS, RogueClassString_IntegerTable* other_0 );
RogueClassString_IntegerTable* RogueString_IntegerTable__add( RogueClassString_IntegerTable* THIS, RogueClassString_IntegerTable* other_0 );
RogueInteger RogueString_IntegerTable__at( RogueClassString_IntegerTable* THIS, RogueInteger index_0 );
void RogueString_IntegerTable__clear( RogueClassString_IntegerTable* THIS );
RogueClassString_IntegerTable* RogueString_IntegerTable__clone( RogueClassString_IntegerTable* THIS );
RogueLogical RogueString_IntegerTable__contains( RogueClassString_IntegerTable* THIS, RogueString* key_0 );
RogueInteger RogueString_IntegerTable__count( RogueClassString_IntegerTable* THIS );
RogueClassString_IntegerTableEntry* RogueString_IntegerTable__find( RogueClassString_IntegerTable* THIS, RogueString* key_0 );
RogueInteger RogueString_IntegerTable__get( RogueClassString_IntegerTable* THIS, RogueString* key_0 );
RogueInteger RogueString_IntegerTable__remove( RogueClassString_IntegerTable* THIS, RogueString* key_0 );
void RogueString_IntegerTable__set( RogueClassString_IntegerTable* THIS, RogueString* key_0, RogueInteger value_1 );
RogueStringBuilder* RogueString_IntegerTable__print_to( RogueClassString_IntegerTable* THIS, RogueStringBuilder* buffer_0 );
RogueClassString_IntegerTable* RogueString_IntegerTable__init_object( RogueClassString_IntegerTable* THIS );
RogueString* RogueAttribute__type_name( RogueClassAttribute* THIS );
RogueClassAttribute* RogueAttribute__init_object( RogueClassAttribute* THIS );
RogueString* RogueTypeArray__type_name( RogueArray* THIS );
RogueString* RogueAttributes__type_name( RogueClassAttributes* THIS );
RogueClassAttributes* RogueAttributes__init( RogueClassAttributes* THIS, RogueInteger _auto_50_0 );
RogueClassAttributes* RogueAttributes__clone( RogueClassAttributes* THIS );
RogueClassAttributes* RogueAttributes__add( RogueClassAttributes* THIS, RogueInteger flag_0 );
RogueClassAttributes* RogueAttributes__add( RogueClassAttributes* THIS, RogueString* tag_0 );
RogueClassAttributes* RogueAttributes__add( RogueClassAttributes* THIS, RogueClassAttributes* other_0 );
RogueString* RogueAttributes__element_type_name( RogueClassAttributes* THIS );
RogueClassAttributes* RogueAttributes__init_object( RogueClassAttributes* THIS );
RogueString* RogueMethod__to_String( RogueClassMethod* THIS );
RogueString* RogueMethod__type_name( RogueClassMethod* THIS );
RogueClassMethod* RogueMethod__init( RogueClassMethod* THIS, RogueClassToken* _auto_51_0, RogueClassType* _auto_52_1, RogueString* _auto_53_2 );
RogueClassMethod* RogueMethod__clone( RogueClassMethod* THIS );
RogueClassMethod* RogueMethod__incorporate( RogueClassMethod* THIS, RogueClassType* into_type_0 );
RogueLogical RogueMethod__accepts_arg_count( RogueClassMethod* THIS, RogueInteger n_0 );
RogueClassLocal* RogueMethod__add_local( RogueClassMethod* THIS, RogueClassToken* v_t_0, RogueString* v_name_1, RogueClassType* v_type_2, RogueClassCmd* v_initial_value_3 );
RogueClassLocal* RogueMethod__add_parameter( RogueClassMethod* THIS, RogueClassToken* p_t_0, RogueString* p_name_1, RogueClassType* p_type_2 );
void RogueMethod__assign_cpp_name( RogueClassMethod* THIS );
void RogueMethod__assign_signature( RogueClassMethod* THIS );
RogueClassMethod* RogueMethod__compile_target( RogueClassMethod* THIS );
RogueLogical RogueMethod__is_dynamic( RogueClassMethod* THIS );
RogueLogical RogueMethod__is_generated( RogueClassMethod* THIS );
RogueLogical RogueMethod__is_incorporated( RogueClassMethod* THIS );
RogueLogical RogueMethod__is_initializer( RogueClassMethod* THIS );
RogueLogical RogueMethod__is_inline( RogueClassMethod* THIS );
RogueLogical RogueMethod__is_native( RogueClassMethod* THIS );
RogueLogical RogueMethod__is_overridden( RogueClassMethod* THIS );
RogueLogical RogueMethod__is_routine( RogueClassMethod* THIS );
RogueLogical RogueMethod__is_task( RogueClassMethod* THIS );
RogueLogical RogueMethod__is_task_conversion( RogueClassMethod* THIS );
RogueLogical RogueMethod__omit_output( RogueClassMethod* THIS );
RogueClassMethod* RogueMethod__organize( RogueClassMethod* THIS, RogueLogical add_to_lookup_0 );
void Rogue_Method__print_prototype( RogueClassMethod* THIS, RogueClassCPPWriter* writer_0 );
void RogueMethod__print_signature( RogueClassMethod* THIS, RogueClassCPPWriter* writer_0 );
void RogueMethod__print_definition( RogueClassMethod* THIS, RogueClassCPPWriter* writer_0 );
void RogueMethod__resolve( RogueClassMethod* THIS );
void RogueMethod__convert_to_task( RogueClassMethod* THIS );
RogueClassMethod* RogueMethod__set_incorporated( RogueClassMethod* THIS );
RogueClassMethod* RogueMethod__set_type_context( RogueClassMethod* THIS, RogueClassType* _auto_54_0 );
RogueClassMethod* RogueMethod__init_object( RogueClassMethod* THIS );
RogueString* RogueCmd__type_name( RogueClassCmd* THIS );
RogueString* RogueCmd__access_name( RogueClassCmd* THIS );
RogueString* RogueCmd__access_cpp_name( RogueClassCmd* THIS );
RogueClassCmd* RogueCmd__call_prior( RogueClassCmd* THIS, RogueClassScope* scope_0 );
RogueClassCmd* RogueCmd__cast_to( RogueClassCmd* THIS, RogueClassType* target_type_0 );
RogueClassCmd* RogueCmd__clone( RogueClassCmd* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmd__clone( RogueClassCmd* THIS, RogueClassCmd* other_0, RogueClassCloneArgs* clone_args_1 );
RogueClassCmdArgs* RogueCmd__clone( RogueClassCmd* THIS, RogueClassCmdArgs* args_0, RogueClassCloneArgs* clone_args_1 );
RogueClassCmdStatementList* RogueCmd__clone( RogueClassCmd* THIS, RogueClassCmdStatementList* statements_0, RogueClassCloneArgs* clone_args_1 );
RogueClassCmd* RogueCmd__combine_literal_operands( RogueClassCmd* THIS, RogueClassType* common_type_0 );
RogueClassType* Rogue_Cmd__compile_type( RogueClassCmd* THIS );
void RogueCmd__exit_scope( RogueClassCmd* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_Cmd__find_operation_result_type( RogueClassCmd* THIS, RogueClassType* left_type_0, RogueClassType* right_type_1 );
RogueClassType* Rogue_Cmd__find_common_type( RogueClassCmd* THIS, RogueClassType* left_type_0, RogueClassType* right_type_1 );
RogueClassType* Rogue_Cmd__must_find_common_type( RogueClassCmd* THIS, RogueClassType* left_type_0, RogueClassType* right_type_1 );
RogueClassType* Rogue_Cmd__implicit_type( RogueClassCmd* THIS );
RogueLogical RogueCmd__is_literal( RogueClassCmd* THIS );
void RogueCmd__write_cpp( RogueClassCmd* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
void RogueCmd__require_type_context( RogueClassCmd* THIS );
RogueClassCmd* RogueCmd__require_integer( RogueClassCmd* THIS );
RogueClassCmd* RogueCmd__require_logical( RogueClassCmd* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_Cmd__require_type( RogueClassCmd* THIS );
RogueClassCmd* RogueCmd__require_value( RogueClassCmd* THIS );
RogueLogical RogueCmd__requires_semicolon( RogueClassCmd* THIS );
RogueClassCmd* RogueCmd__resolve( RogueClassCmd* THIS, RogueClassScope* scope_0 );
RogueClassCmd* RogueCmd__resolve_assignment( RogueClassCmd* THIS, RogueClassScope* scope_0, RogueClassCmd* new_value_1 );
RogueClassCmd* RogueCmd__resolve_modify( RogueClassCmd* THIS, RogueClassScope* scope_0, RogueInteger delta_1 );
RogueClassCmd* RogueCmd__resolve_modify_and_assign( RogueClassCmd* THIS, RogueClassScope* scope_0, RogueClassTokenType* op_1, RogueClassCmd* new_value_2 );
RogueClassType* Rogue_Cmd__type( RogueClassCmd* THIS );
RogueClassCmd* RogueCmd__init_object( RogueClassCmd* THIS );
RogueString* RogueCmdReturn__type_name( RogueClassCmdReturn* THIS );
RogueClassCmd* RogueCmdReturn__clone( RogueClassCmdReturn* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdReturn__write_cpp( RogueClassCmdReturn* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdReturn__resolve( RogueClassCmdReturn* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdReturn__type( RogueClassCmdReturn* THIS );
RogueClassCmdReturn* RogueCmdReturn__init_object( RogueClassCmdReturn* THIS );
RogueClassCmdReturn* RogueCmdReturn__init( RogueClassCmdReturn* THIS, RogueClassToken* _auto_56_0, RogueClassCmd* _auto_57_1 );
RogueString* RogueCmdStatement__type_name( RogueClassCmdStatement* THIS );
RogueClassCmdStatement* RogueCmdStatement__init_object( RogueClassCmdStatement* THIS );
RogueString* RogueCmdStatementList__type_name( RogueClassCmdStatementList* THIS );
RogueClassCmdStatementList* RogueCmdStatementList__init_object( RogueClassCmdStatementList* THIS );
RogueClassCmdStatementList* RogueCmdStatementList__init( RogueClassCmdStatementList* THIS );
RogueClassCmdStatementList* RogueCmdStatementList__init( RogueClassCmdStatementList* THIS, RogueInteger initial_capacity_0 );
RogueClassCmdStatementList* RogueCmdStatementList__init( RogueClassCmdStatementList* THIS, RogueClassCmd* statement_0 );
RogueClassCmdStatementList* RogueCmdStatementList__init( RogueClassCmdStatementList* THIS, RogueClassCmd* statement1_0, RogueClassCmd* statement2_1 );
RogueClassCmdStatementList* RogueCmdStatementList__clone( RogueClassCmdStatementList* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdStatementList__write_cpp( RogueClassCmdStatementList* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
void RogueCmdStatementList__resolve( RogueClassCmdStatementList* THIS, RogueClassScope* scope_0 );
RogueString* RogueCmdList__to_String( RogueCmdList* THIS );
RogueString* RogueCmdList__type_name( RogueCmdList* THIS );
RogueCmdList* RogueCmdList__init_object( RogueCmdList* THIS );
RogueCmdList* RogueCmdList__init( RogueCmdList* THIS );
RogueCmdList* RogueCmdList__init( RogueCmdList* THIS, RogueInteger initial_capacity_0 );
RogueCmdList* RogueCmdList__init( RogueCmdList* THIS, RogueInteger initial_capacity_0, RogueClassCmd* initial_value_1 );
RogueCmdList* RogueCmdList__clone( RogueCmdList* THIS );
RogueCmdList* RogueCmdList__add( RogueCmdList* THIS, RogueClassCmd* value_0 );
RogueCmdList* RogueCmdList__add( RogueCmdList* THIS, RogueCmdList* other_0 );
RogueInteger RogueCmdList__capacity( RogueCmdList* THIS );
RogueCmdList* RogueCmdList__clear( RogueCmdList* THIS );
RogueCmdList* RogueCmdList__insert( RogueCmdList* THIS, RogueClassCmd* value_0, RogueInteger before_index_1 );
RogueClassCmd* RogueCmdList__last( RogueCmdList* THIS );
RogueClassCmd* RogueCmdList__random( RogueCmdList* THIS );
RogueCmdList* RogueCmdList__reserve( RogueCmdList* THIS, RogueInteger additional_count_0 );
RogueClassCmd* RogueCmdList__remove( RogueCmdList* THIS, RogueClassCmd* value_0 );
RogueClassCmd* RogueCmdList__remove_at( RogueCmdList* THIS, RogueInteger index_0 );
RogueClassCmd* RogueCmdList__remove_last( RogueCmdList* THIS );
RogueString* RogueTokenType__to_String( RogueClassTokenType* THIS );
RogueString* RogueTokenType__type_name( RogueClassTokenType* THIS );
RogueClassTokenType* RogueTokenType__init( RogueClassTokenType* THIS, RogueString* _auto_59_0 );
RogueClassToken* RogueTokenType__create_token( RogueClassTokenType* THIS, RogueString* filepath_0, RogueInteger line_1, RogueInteger column_2 );
RogueClassToken* RogueTokenType__create_token( RogueClassTokenType* THIS, RogueString* filepath_0, RogueInteger line_1, RogueInteger column_2, RogueCharacter value_3 );
RogueClassToken* RogueTokenType__create_token( RogueClassTokenType* THIS, RogueString* filepath_0, RogueInteger line_1, RogueInteger column_2, RogueInteger value_3 );
RogueClassToken* RogueTokenType__create_token( RogueClassTokenType* THIS, RogueString* filepath_0, RogueInteger line_1, RogueInteger column_2, RogueReal value_3 );
RogueClassToken* RogueTokenType__create_token( RogueClassTokenType* THIS, RogueString* filepath_0, RogueInteger line_1, RogueInteger column_2, RogueString* value_3 );
RogueClassToken* RogueTokenType__create_token( RogueClassTokenType* THIS, RogueClassToken* existing_0, RogueString* value_1 );
RogueClassToken* RogueTokenType__create_token( RogueClassTokenType* THIS, RogueClassToken* existing_0, RogueClassType* original_type_1, RogueClassType* target_type_2 );
RogueLogical RogueTokenType__is_directive( RogueClassTokenType* THIS );
RogueLogical RogueTokenType__is_op_with_assign( RogueClassTokenType* THIS );
RogueLogical RogueTokenType__is_structure( RogueClassTokenType* THIS );
RogueString* RogueTokenType__quoted_name( RogueClassTokenType* THIS );
RogueString* RogueTokenType__to_String( RogueClassTokenType* THIS, RogueClassToken* t_0 );
RogueClassTokenType* RogueTokenType__init_object( RogueClassTokenType* THIS );
RogueString* RogueTemplateArray__type_name( RogueArray* THIS );
RogueString* RogueMethodList__to_String( RogueMethodList* THIS );
RogueString* RogueMethodList__type_name( RogueMethodList* THIS );
RogueMethodList* RogueMethodList__init_object( RogueMethodList* THIS );
RogueMethodList* RogueMethodList__init( RogueMethodList* THIS );
RogueMethodList* RogueMethodList__init( RogueMethodList* THIS, RogueInteger initial_capacity_0 );
RogueMethodList* RogueMethodList__init( RogueMethodList* THIS, RogueInteger initial_capacity_0, RogueClassMethod* initial_value_1 );
RogueMethodList* RogueMethodList__clone( RogueMethodList* THIS );
RogueMethodList* RogueMethodList__add( RogueMethodList* THIS, RogueClassMethod* value_0 );
RogueMethodList* RogueMethodList__add( RogueMethodList* THIS, RogueMethodList* other_0 );
RogueInteger RogueMethodList__capacity( RogueMethodList* THIS );
RogueMethodList* RogueMethodList__clear( RogueMethodList* THIS );
RogueMethodList* RogueMethodList__insert( RogueMethodList* THIS, RogueClassMethod* value_0, RogueInteger before_index_1 );
RogueClassMethod* RogueMethodList__last( RogueMethodList* THIS );
RogueClassMethod* RogueMethodList__random( RogueMethodList* THIS );
RogueMethodList* RogueMethodList__reserve( RogueMethodList* THIS, RogueInteger additional_count_0 );
RogueClassMethod* RogueMethodList__remove( RogueMethodList* THIS, RogueClassMethod* value_0 );
RogueClassMethod* RogueMethodList__remove_at( RogueMethodList* THIS, RogueInteger index_0 );
RogueClassMethod* RogueMethodList__remove_last( RogueMethodList* THIS );
RogueString* RogueMethodArray__type_name( RogueArray* THIS );
RogueString* RogueCPPWriter__type_name( RogueClassCPPWriter* THIS );
RogueClassCPPWriter* RogueCPPWriter__init( RogueClassCPPWriter* THIS, RogueString* _auto_77_0 );
void RogueCPPWriter__close( RogueClassCPPWriter* THIS );
void RogueCPPWriter__print_indent( RogueClassCPPWriter* THIS );
RogueClassCPPWriter* RogueCPPWriter__print( RogueClassCPPWriter* THIS, RogueInteger value_0 );
RogueClassCPPWriter* RogueCPPWriter__print( RogueClassCPPWriter* THIS, RogueReal value_0 );
RogueClassCPPWriter* RogueCPPWriter__print( RogueClassCPPWriter* THIS, RogueString* value_0 );
RogueClassCPPWriter* RogueCPPWriter__println( RogueClassCPPWriter* THIS );
RogueClassCPPWriter* RogueCPPWriter__println( RogueClassCPPWriter* THIS, RogueInteger value_0 );
RogueClassCPPWriter* RogueCPPWriter__println( RogueClassCPPWriter* THIS, RogueReal value_0 );
RogueClassCPPWriter* RogueCPPWriter__println( RogueClassCPPWriter* THIS, RogueString* value_0 );
RogueClassCPPWriter* RogueCPPWriter__print( RogueClassCPPWriter* THIS, RogueClassType* type_0 );
RogueClassCPPWriter* RogueCPPWriter__print_cast( RogueClassCPPWriter* THIS, RogueClassType* from_type_0, RogueClassType* to_type_1 );
RogueClassCPPWriter* RogueCPPWriter__print_open_cast( RogueClassCPPWriter* THIS, RogueClassType* from_type_0, RogueClassType* to_type_1 );
RogueClassCPPWriter* RogueCPPWriter__print_close_cast( RogueClassCPPWriter* THIS, RogueClassType* from_type_0, RogueClassType* to_type_1 );
RogueClassCPPWriter* RogueCPPWriter__print_cast( RogueClassCPPWriter* THIS, RogueClassType* from_type_0, RogueClassType* to_type_1, RogueClassCmd* cmd_2 );
RogueClassCPPWriter* RogueCPPWriter__print_access_operator( RogueClassCPPWriter* THIS, RogueClassType* type_0 );
RogueClassCPPWriter* RogueCPPWriter__print_type_name( RogueClassCPPWriter* THIS, RogueClassType* type_0 );
RogueClassCPPWriter* RogueCPPWriter__print_type_info( RogueClassCPPWriter* THIS, RogueClassType* type_0 );
RogueClassCPPWriter* RogueCPPWriter__print_default_value( RogueClassCPPWriter* THIS, RogueClassType* type_0 );
RogueClassCPPWriter* RogueCPPWriter__print( RogueClassCPPWriter* THIS, RogueCharacter ch_0, RogueLogical in_string_1 );
RogueClassCPPWriter* RogueCPPWriter__print_string_utf8( RogueClassCPPWriter* THIS, RogueString* st_0 );
RogueClassCPPWriter* RogueCPPWriter__init_object( RogueClassCPPWriter* THIS );
RogueString* RogueFileReader__type_name( RogueFileReader* THIS );
RogueInteger RogueFileReader__remaining( RogueFileReader* THIS );
RogueLogical RogueCharacterReader__has_another( RogueObject* THIS );
RogueCharacter RogueCharacterReader__peek( RogueObject* THIS );
RogueCharacter RogueCharacterReader__read( RogueObject* THIS );
RogueString* RogueCharacterReader__type_name( RogueObject* THIS );
RogueString* RogueString_MethodTable__to_String( RogueClassString_MethodTable* THIS );
RogueString* RogueString_MethodTable__type_name( RogueClassString_MethodTable* THIS );
RogueClassString_MethodTable* RogueString_MethodTable__init( RogueClassString_MethodTable* THIS );
RogueClassString_MethodTable* RogueString_MethodTable__init( RogueClassString_MethodTable* THIS, RogueInteger bin_count_0 );
RogueClassString_MethodTable* RogueString_MethodTable__init( RogueClassString_MethodTable* THIS, RogueClassString_MethodTable* other_0 );
RogueClassString_MethodTable* RogueString_MethodTable__add( RogueClassString_MethodTable* THIS, RogueClassString_MethodTable* other_0 );
RogueClassMethod* RogueString_MethodTable__at( RogueClassString_MethodTable* THIS, RogueInteger index_0 );
void RogueString_MethodTable__clear( RogueClassString_MethodTable* THIS );
RogueClassString_MethodTable* RogueString_MethodTable__clone( RogueClassString_MethodTable* THIS );
RogueLogical RogueString_MethodTable__contains( RogueClassString_MethodTable* THIS, RogueString* key_0 );
RogueInteger RogueString_MethodTable__count( RogueClassString_MethodTable* THIS );
RogueClassString_MethodTableEntry* RogueString_MethodTable__find( RogueClassString_MethodTable* THIS, RogueString* key_0 );
RogueClassMethod* RogueString_MethodTable__get( RogueClassString_MethodTable* THIS, RogueString* key_0 );
RogueClassMethod* RogueString_MethodTable__remove( RogueClassString_MethodTable* THIS, RogueString* key_0 );
void RogueString_MethodTable__set( RogueClassString_MethodTable* THIS, RogueString* key_0, RogueClassMethod* value_1 );
RogueStringBuilder* RogueString_MethodTable__print_to( RogueClassString_MethodTable* THIS, RogueStringBuilder* buffer_0 );
RogueClassString_MethodTable* RogueString_MethodTable__init_object( RogueClassString_MethodTable* THIS );
RogueString* RogueLocalList__to_String( RogueLocalList* THIS );
RogueString* RogueLocalList__type_name( RogueLocalList* THIS );
RogueLocalList* RogueLocalList__init_object( RogueLocalList* THIS );
RogueLocalList* RogueLocalList__init( RogueLocalList* THIS );
RogueLocalList* RogueLocalList__init( RogueLocalList* THIS, RogueInteger initial_capacity_0 );
RogueLocalList* RogueLocalList__init( RogueLocalList* THIS, RogueInteger initial_capacity_0, RogueClassLocal* initial_value_1 );
RogueLocalList* RogueLocalList__clone( RogueLocalList* THIS );
RogueLocalList* RogueLocalList__add( RogueLocalList* THIS, RogueClassLocal* value_0 );
RogueLocalList* RogueLocalList__add( RogueLocalList* THIS, RogueLocalList* other_0 );
RogueInteger RogueLocalList__capacity( RogueLocalList* THIS );
RogueLocalList* RogueLocalList__clear( RogueLocalList* THIS );
RogueLocalList* RogueLocalList__insert( RogueLocalList* THIS, RogueClassLocal* value_0, RogueInteger before_index_1 );
RogueClassLocal* RogueLocalList__last( RogueLocalList* THIS );
RogueClassLocal* RogueLocalList__random( RogueLocalList* THIS );
RogueLocalList* RogueLocalList__reserve( RogueLocalList* THIS, RogueInteger additional_count_0 );
RogueClassLocal* RogueLocalList__remove( RogueLocalList* THIS, RogueClassLocal* value_0 );
RogueClassLocal* RogueLocalList__remove_at( RogueLocalList* THIS, RogueInteger index_0 );
RogueClassLocal* RogueLocalList__remove_last( RogueLocalList* THIS );
RogueString* RogueLocal__type_name( RogueClassLocal* THIS );
RogueClassLocal* RogueLocal__init( RogueClassLocal* THIS, RogueClassToken* _auto_98_0, RogueString* _auto_99_1 );
RogueClassLocal* RogueLocal__clone( RogueClassLocal* THIS, RogueClassCloneArgs* clone_args_0 );
RogueString* RogueLocal__cpp_name( RogueClassLocal* THIS );
RogueClassLocal* RogueLocal__init_object( RogueClassLocal* THIS );
RogueString* RogueLocalArray__type_name( RogueArray* THIS );
RogueString* RogueTaskManager__type_name( RogueClassTaskManager* THIS );
RogueClassTaskManager* RogueTaskManager__add( RogueClassTaskManager* THIS, RogueClassTask* task_0 );
RogueClassTask* RogueTaskManager__await_all( RogueClassTaskManager* THIS, RogueTaskList* tasks_0 );
void RogueTaskManager__dispatch_events( RogueClassTaskManager* THIS );
RogueLogical RogueTaskManager__update( RogueClassTaskManager* THIS );
RogueClassTaskManager* RogueTaskManager__init_object( RogueClassTaskManager* THIS );
RogueString* RogueTask__type_name( RogueClassTask* THIS );
RogueClassTask* RogueTask__start( RogueClassTask* THIS );
RogueLogical RogueTask__update( RogueClassTask* THIS );
RogueClassTask* RogueTask__init_object( RogueClassTask* THIS );
RogueString* RogueTaskList__to_String( RogueTaskList* THIS );
RogueString* RogueTaskList__type_name( RogueTaskList* THIS );
RogueTaskList* RogueTaskList__init_object( RogueTaskList* THIS );
RogueTaskList* RogueTaskList__init( RogueTaskList* THIS );
RogueTaskList* RogueTaskList__init( RogueTaskList* THIS, RogueInteger initial_capacity_0 );
RogueTaskList* RogueTaskList__init( RogueTaskList* THIS, RogueInteger initial_capacity_0, RogueClassTask* initial_value_1 );
RogueTaskList* RogueTaskList__clone( RogueTaskList* THIS );
RogueTaskList* RogueTaskList__add( RogueTaskList* THIS, RogueClassTask* value_0 );
RogueTaskList* RogueTaskList__add( RogueTaskList* THIS, RogueTaskList* other_0 );
RogueInteger RogueTaskList__capacity( RogueTaskList* THIS );
RogueTaskList* RogueTaskList__clear( RogueTaskList* THIS );
RogueTaskList* RogueTaskList__insert( RogueTaskList* THIS, RogueClassTask* value_0, RogueInteger before_index_1 );
RogueClassTask* RogueTaskList__last( RogueTaskList* THIS );
RogueClassTask* RogueTaskList__random( RogueTaskList* THIS );
RogueTaskList* RogueTaskList__reserve( RogueTaskList* THIS, RogueInteger additional_count_0 );
RogueClassTask* RogueTaskList__remove( RogueTaskList* THIS, RogueClassTask* value_0 );
RogueClassTask* RogueTaskList__remove_at( RogueTaskList* THIS, RogueInteger index_0 );
RogueClassTask* RogueTaskList__remove_last( RogueTaskList* THIS );
RogueString* RogueTaskArray__type_name( RogueArray* THIS );
RogueString* RogueTaskManager__await_all__task127__type_name( RogueClassTaskManager__await_all__task127* THIS );
RogueLogical RogueTaskManager__await_all__task127__update( RogueClassTaskManager__await_all__task127* THIS );
RogueClassTaskManager__await_all__task127* RogueTaskManager__await_all__task127__init_object( RogueClassTaskManager__await_all__task127* THIS );
RogueClassTaskManager__await_all__task127* RogueTaskManager__await_all__task127__init( RogueClassTaskManager__await_all__task127* THIS, RogueClassTaskManager* _auto_129_0, RogueTaskList* _auto_130_1 );
RogueLogical RogueTaskManager__await_all__task127__execute( RogueClassTaskManager__await_all__task127* THIS );
RogueString* RogueSystemEventQueue__type_name( RogueClassSystemEventQueue* THIS );
RogueClassSystemEventQueue* RogueSystemEventQueue__init_object( RogueClassSystemEventQueue* THIS );
RogueString* RogueEventManager__type_name( RogueClassEventManager* THIS );
RogueInteger RogueEventManager__create_event_id( RogueClassEventManager* THIS );
void RogueEventManager__dispatch_events( RogueClassEventManager* THIS );
RogueClassEventManager* RogueEventManager__init_object( RogueClassEventManager* THIS );
RogueString* RogueCharacterArray__type_name( RogueArray* THIS );
RogueString* RogueRandom__type_name( RogueClassRandom* THIS );
RogueClassRandom* RogueRandom__init( RogueClassRandom* THIS );
RogueClassRandom* RogueRandom__init( RogueClassRandom* THIS, RogueLong seed_0 );
RogueLong RogueRandom__advance_state( RogueClassRandom* THIS );
void RogueRandom__reset( RogueClassRandom* THIS, RogueLong _auto_139_0 );
RogueReal RogueRandom__next_real( RogueClassRandom* THIS );
RogueReal RogueRandom__next_real( RogueClassRandom* THIS, RogueReal low_0, RogueReal high_1 );
RogueInteger RogueRandom__next_integer( RogueClassRandom* THIS );
RogueInteger RogueRandom__next_integer( RogueClassRandom* THIS, RogueInteger limit_0 );
RogueInteger RogueRandom__next_integer( RogueClassRandom* THIS, RogueInteger low_0, RogueInteger high_1 );
RogueLogical RogueRandom__next_logical( RogueClassRandom* THIS );
RogueClassRandom* RogueRandom__init_object( RogueClassRandom* THIS );
RogueString* RogueCharacterListOps__type_name( RogueClassCharacterListOps* THIS );
RogueClassCharacterListOps* RogueCharacterListOps__init_object( RogueClassCharacterListOps* THIS );
RogueString* RogueString_LogicalTableEntryList__to_String( RogueString_LogicalTableEntryList* THIS );
RogueString* RogueString_LogicalTableEntryList__type_name( RogueString_LogicalTableEntryList* THIS );
RogueString_LogicalTableEntryList* RogueString_LogicalTableEntryList__init_object( RogueString_LogicalTableEntryList* THIS );
RogueString_LogicalTableEntryList* RogueString_LogicalTableEntryList__init( RogueString_LogicalTableEntryList* THIS );
RogueString_LogicalTableEntryList* RogueString_LogicalTableEntryList__init( RogueString_LogicalTableEntryList* THIS, RogueInteger initial_capacity_0 );
RogueString_LogicalTableEntryList* RogueString_LogicalTableEntryList__init( RogueString_LogicalTableEntryList* THIS, RogueInteger initial_capacity_0, RogueClassString_LogicalTableEntry* initial_value_1 );
RogueString_LogicalTableEntryList* RogueString_LogicalTableEntryList__clone( RogueString_LogicalTableEntryList* THIS );
RogueString_LogicalTableEntryList* RogueString_LogicalTableEntryList__add( RogueString_LogicalTableEntryList* THIS, RogueClassString_LogicalTableEntry* value_0 );
RogueString_LogicalTableEntryList* RogueString_LogicalTableEntryList__add( RogueString_LogicalTableEntryList* THIS, RogueString_LogicalTableEntryList* other_0 );
RogueInteger RogueString_LogicalTableEntryList__capacity( RogueString_LogicalTableEntryList* THIS );
RogueString_LogicalTableEntryList* RogueString_LogicalTableEntryList__clear( RogueString_LogicalTableEntryList* THIS );
RogueString_LogicalTableEntryList* RogueString_LogicalTableEntryList__insert( RogueString_LogicalTableEntryList* THIS, RogueClassString_LogicalTableEntry* value_0, RogueInteger before_index_1 );
RogueClassString_LogicalTableEntry* RogueString_LogicalTableEntryList__last( RogueString_LogicalTableEntryList* THIS );
RogueClassString_LogicalTableEntry* RogueString_LogicalTableEntryList__random( RogueString_LogicalTableEntryList* THIS );
RogueString_LogicalTableEntryList* RogueString_LogicalTableEntryList__reserve( RogueString_LogicalTableEntryList* THIS, RogueInteger additional_count_0 );
RogueClassString_LogicalTableEntry* RogueString_LogicalTableEntryList__remove( RogueString_LogicalTableEntryList* THIS, RogueClassString_LogicalTableEntry* value_0 );
RogueClassString_LogicalTableEntry* RogueString_LogicalTableEntryList__remove_at( RogueString_LogicalTableEntryList* THIS, RogueInteger index_0 );
RogueClassString_LogicalTableEntry* RogueString_LogicalTableEntryList__remove_last( RogueString_LogicalTableEntryList* THIS );
RogueString* RogueString_LogicalTableEntry__type_name( RogueClassString_LogicalTableEntry* THIS );
RogueClassString_LogicalTableEntry* RogueString_LogicalTableEntry__init( RogueClassString_LogicalTableEntry* THIS, RogueString* _key_0, RogueLogical _value_1, RogueInteger _hash_2 );
RogueClassString_LogicalTableEntry* RogueString_LogicalTableEntry__init_object( RogueClassString_LogicalTableEntry* THIS );
RogueString* RogueString_LogicalTableEntryArray__type_name( RogueArray* THIS );
RogueString* RogueByteList__to_String( RogueByteList* THIS );
RogueString* RogueByteList__type_name( RogueByteList* THIS );
RogueByteList* RogueByteList__init_object( RogueByteList* THIS );
RogueByteList* RogueByteList__init( RogueByteList* THIS );
RogueByteList* RogueByteList__init( RogueByteList* THIS, RogueInteger initial_capacity_0 );
RogueByteList* RogueByteList__init( RogueByteList* THIS, RogueInteger initial_capacity_0, RogueByte initial_value_1 );
RogueByteList* RogueByteList__clone( RogueByteList* THIS );
RogueByteList* RogueByteList__add( RogueByteList* THIS, RogueByte value_0 );
RogueByteList* RogueByteList__add( RogueByteList* THIS, RogueByteList* other_0 );
RogueInteger RogueByteList__capacity( RogueByteList* THIS );
RogueByteList* RogueByteList__clear( RogueByteList* THIS );
RogueByteList* RogueByteList__insert( RogueByteList* THIS, RogueByte value_0, RogueInteger before_index_1 );
RogueByte RogueByteList__last( RogueByteList* THIS );
RogueByte RogueByteList__random( RogueByteList* THIS );
RogueByteList* RogueByteList__reserve( RogueByteList* THIS, RogueInteger additional_count_0 );
RogueByte RogueByteList__remove( RogueByteList* THIS, RogueByte value_0 );
RogueByte RogueByteList__remove_at( RogueByteList* THIS, RogueInteger index_0 );
RogueByte RogueByteList__remove_last( RogueByteList* THIS );
RogueString* RogueFileWriter__type_name( RogueFileWriter* THIS );
RogueString* RogueTokenReader__type_name( RogueClassTokenReader* THIS );
RogueClassTokenReader* RogueTokenReader__init( RogueClassTokenReader* THIS, RogueTokenList* _auto_157_0 );
RogueClassError* RogueTokenReader__error( RogueClassTokenReader* THIS, RogueString* message_0 );
RogueLogical RogueTokenReader__has_another( RogueClassTokenReader* THIS );
RogueLogical RogueTokenReader__next_is( RogueClassTokenReader* THIS, RogueClassTokenType* type_0 );
RogueLogical RogueTokenReader__next_is_statement_token( RogueClassTokenReader* THIS );
RogueClassToken* RogueTokenReader__peek( RogueClassTokenReader* THIS );
RogueClassToken* RogueTokenReader__peek( RogueClassTokenReader* THIS, RogueInteger num_ahead_0 );
RogueClassToken* RogueTokenReader__read( RogueClassTokenReader* THIS );
RogueClassTokenReader* RogueTokenReader__init_object( RogueClassTokenReader* THIS );
RogueString* RoguePropertyList__to_String( RoguePropertyList* THIS );
RogueString* RoguePropertyList__type_name( RoguePropertyList* THIS );
RoguePropertyList* RoguePropertyList__init_object( RoguePropertyList* THIS );
RoguePropertyList* RoguePropertyList__init( RoguePropertyList* THIS );
RoguePropertyList* RoguePropertyList__init( RoguePropertyList* THIS, RogueInteger initial_capacity_0 );
RoguePropertyList* RoguePropertyList__init( RoguePropertyList* THIS, RogueInteger initial_capacity_0, RogueClassProperty* initial_value_1 );
RoguePropertyList* RoguePropertyList__clone( RoguePropertyList* THIS );
RoguePropertyList* RoguePropertyList__add( RoguePropertyList* THIS, RogueClassProperty* value_0 );
RoguePropertyList* RoguePropertyList__add( RoguePropertyList* THIS, RoguePropertyList* other_0 );
RogueInteger RoguePropertyList__capacity( RoguePropertyList* THIS );
RoguePropertyList* RoguePropertyList__clear( RoguePropertyList* THIS );
RoguePropertyList* RoguePropertyList__insert( RoguePropertyList* THIS, RogueClassProperty* value_0, RogueInteger before_index_1 );
RogueClassProperty* RoguePropertyList__last( RoguePropertyList* THIS );
RogueClassProperty* RoguePropertyList__random( RoguePropertyList* THIS );
RoguePropertyList* RoguePropertyList__reserve( RoguePropertyList* THIS, RogueInteger additional_count_0 );
RogueClassProperty* RoguePropertyList__remove( RoguePropertyList* THIS, RogueClassProperty* value_0 );
RogueClassProperty* RoguePropertyList__remove_at( RoguePropertyList* THIS, RogueInteger index_0 );
RogueClassProperty* RoguePropertyList__remove_last( RoguePropertyList* THIS );
RogueString* RogueProperty__to_String( RogueClassProperty* THIS );
RogueString* RogueProperty__type_name( RogueClassProperty* THIS );
RogueClassProperty* RogueProperty__init( RogueClassProperty* THIS, RogueClassToken* _auto_158_0, RogueClassType* _auto_159_1, RogueString* _auto_160_2 );
RogueClassProperty* RogueProperty__clone( RogueClassProperty* THIS );
RogueClassProperty* RogueProperty__set_type_context( RogueClassProperty* THIS, RogueClassType* _auto_161_0 );
RogueClassProperty* RogueProperty__init_object( RogueClassProperty* THIS );
RogueString* RogueTokenizer__type_name( RogueClassTokenizer* THIS );
RogueTokenList* RogueTokenizer__tokenize( RogueClassTokenizer* THIS, RogueString* _auto_163_0 );
RogueTokenList* RogueTokenizer__tokenize( RogueClassTokenizer* THIS, RogueClassToken* reference_t_0, RogueString* _auto_164_1, RogueString* data_2 );
RogueTokenList* RogueTokenizer__tokenize( RogueClassTokenizer* THIS, RogueClassParseReader* _auto_165_0 );
RogueLogical RogueTokenizer__add_new_string_or_character_token_from_buffer( RogueClassTokenizer* THIS, RogueCharacter terminator_0 );
RogueLogical RogueTokenizer__add_new_token( RogueClassTokenizer* THIS, RogueClassTokenType* type_0 );
RogueLogical RogueTokenizer__add_new_token( RogueClassTokenizer* THIS, RogueClassTokenType* type_0, RogueCharacter value_1 );
RogueLogical RogueTokenizer__add_new_token( RogueClassTokenizer* THIS, RogueClassTokenType* type_0, RogueInteger value_1 );
RogueLogical RogueTokenizer__add_new_token( RogueClassTokenizer* THIS, RogueClassTokenType* type_0, RogueReal value_1 );
RogueLogical RogueTokenizer__add_new_token( RogueClassTokenizer* THIS, RogueClassTokenType* type_0, RogueString* value_1 );
void RogueTokenizer__configure_token_types( RogueClassTokenizer* THIS );
RogueLogical RogueTokenizer__consume( RogueClassTokenizer* THIS, RogueCharacter ch_0 );
RogueLogical RogueTokenizer__consume( RogueClassTokenizer* THIS, RogueString* st_0 );
RogueLogical RogueTokenizer__consume_id( RogueClassTokenizer* THIS, RogueString* st_0 );
RogueLogical RogueTokenizer__consume_spaces( RogueClassTokenizer* THIS );
RogueClassTokenType* RogueTokenizer__define( RogueClassTokenizer* THIS, RogueClassTokenType* type_0 );
RogueClassRogueError* RogueTokenizer__error( RogueClassTokenizer* THIS, RogueString* message_0 );
RogueClassTokenType* Rogue_Tokenizer__get_symbol_token_type( RogueClassTokenizer* THIS );
RogueLogical RogueTokenizer__next_is_hex_digit( RogueClassTokenizer* THIS );
RogueCharacter RogueTokenizer__read_character( RogueClassTokenizer* THIS );
RogueCharacter RogueTokenizer__read_hex_value( RogueClassTokenizer* THIS, RogueInteger digits_0 );
RogueString* RogueTokenizer__read_identifier( RogueClassTokenizer* THIS );
RogueLogical RogueTokenizer__tokenize_alternate_string( RogueClassTokenizer* THIS, RogueCharacter terminator_0 );
RogueLogical RogueTokenizer__tokenize_another( RogueClassTokenizer* THIS );
RogueLogical RogueTokenizer__tokenize_comment( RogueClassTokenizer* THIS );
RogueLogical RogueTokenizer__tokenize_integer_in_base( RogueClassTokenizer* THIS, RogueInteger base_0 );
RogueLogical RogueTokenizer__tokenize_number( RogueClassTokenizer* THIS );
RogueReal RogueTokenizer__tokenize_Integer( RogueClassTokenizer* THIS );
RogueLogical RogueTokenizer__tokenize_string( RogueClassTokenizer* THIS, RogueCharacter terminator_0 );
RogueLogical RogueTokenizer__tokenize_verbatim_string( RogueClassTokenizer* THIS );
RogueClassTokenizer* RogueTokenizer__init_object( RogueClassTokenizer* THIS );
RogueString* RogueParseReader__type_name( RogueClassParseReader* THIS );
RogueLogical RogueParseReader__has_another( RogueClassParseReader* THIS );
RogueCharacter RogueParseReader__peek( RogueClassParseReader* THIS );
RogueCharacter RogueParseReader__read( RogueClassParseReader* THIS );
RogueClassParseReader* RogueParseReader__init( RogueClassParseReader* THIS, RogueString* filepath_0 );
RogueClassParseReader* RogueParseReader__init( RogueClassParseReader* THIS, RogueClassFile* file_0 );
RogueClassParseReader* RogueParseReader__init( RogueClassParseReader* THIS, RogueByteList* original_data_0 );
RogueClassParseReader* RogueParseReader__init( RogueClassParseReader* THIS, RogueCharacterList* original_data_0 );
RogueLogical RogueParseReader__consume( RogueClassParseReader* THIS, RogueCharacter ch_0 );
RogueLogical RogueParseReader__consume( RogueClassParseReader* THIS, RogueString* text_0 );
RogueLogical RogueParseReader__consume_id( RogueClassParseReader* THIS, RogueString* text_0 );
RogueLogical RogueParseReader__consume_spaces( RogueClassParseReader* THIS );
RogueCharacter RogueParseReader__peek( RogueClassParseReader* THIS, RogueInteger num_ahead_0 );
RogueInteger RogueParseReader__read_hex_value( RogueClassParseReader* THIS );
RogueClassParseReader* RogueParseReader__seek_to( RogueClassParseReader* THIS, RogueInteger new_line_0, RogueInteger new_column_1 );
RogueClassParseReader* RogueParseReader__set_position( RogueClassParseReader* THIS, RogueInteger _auto_166_0, RogueInteger _auto_167_1 );
RogueClassParseReader* RogueParseReader__init_object( RogueClassParseReader* THIS );
RogueString* RoguePreprocessor__type_name( RogueClassPreprocessor* THIS );
RogueTokenList* RoguePreprocessor__process( RogueClassPreprocessor* THIS, RogueTokenList* _auto_168_0 );
RogueLogical RoguePreprocessor__consume( RogueClassPreprocessor* THIS, RogueClassTokenType* type_0 );
void RoguePreprocessor__process( RogueClassPreprocessor* THIS, RogueLogical keep_tokens_0, RogueInteger depth_1, RogueLogical stop_on_eol_2 );
void RoguePreprocessor__must_consume( RogueClassPreprocessor* THIS, RogueClassTokenType* type_0 );
RogueLogical RoguePreprocessor__parse_logical_expression( RogueClassPreprocessor* THIS );
RogueLogical RoguePreprocessor__parse_logical_or( RogueClassPreprocessor* THIS );
RogueLogical RoguePreprocessor__parse_logical_or( RogueClassPreprocessor* THIS, RogueLogical lhs_0 );
RogueLogical RoguePreprocessor__parse_logical_and( RogueClassPreprocessor* THIS );
RogueLogical RoguePreprocessor__parse_logical_and( RogueClassPreprocessor* THIS, RogueLogical lhs_0 );
RogueLogical RoguePreprocessor__parse_logical_term( RogueClassPreprocessor* THIS );
RogueString* RoguePreprocessor__read_identifier( RogueClassPreprocessor* THIS );
RogueClassPreprocessor* RoguePreprocessor__init_object( RogueClassPreprocessor* THIS );
RogueString* RogueString_TokenTypeTable__to_String( RogueClassString_TokenTypeTable* THIS );
RogueString* RogueString_TokenTypeTable__type_name( RogueClassString_TokenTypeTable* THIS );
RogueClassString_TokenTypeTable* RogueString_TokenTypeTable__init( RogueClassString_TokenTypeTable* THIS );
RogueClassString_TokenTypeTable* RogueString_TokenTypeTable__init( RogueClassString_TokenTypeTable* THIS, RogueInteger bin_count_0 );
RogueClassString_TokenTypeTable* RogueString_TokenTypeTable__init( RogueClassString_TokenTypeTable* THIS, RogueClassString_TokenTypeTable* other_0 );
RogueClassString_TokenTypeTable* RogueString_TokenTypeTable__add( RogueClassString_TokenTypeTable* THIS, RogueClassString_TokenTypeTable* other_0 );
RogueClassTokenType* RogueString_TokenTypeTable__at( RogueClassString_TokenTypeTable* THIS, RogueInteger index_0 );
void RogueString_TokenTypeTable__clear( RogueClassString_TokenTypeTable* THIS );
RogueClassString_TokenTypeTable* RogueString_TokenTypeTable__clone( RogueClassString_TokenTypeTable* THIS );
RogueLogical RogueString_TokenTypeTable__contains( RogueClassString_TokenTypeTable* THIS, RogueString* key_0 );
RogueInteger RogueString_TokenTypeTable__count( RogueClassString_TokenTypeTable* THIS );
RogueClassString_TokenTypeTableEntry* RogueString_TokenTypeTable__find( RogueClassString_TokenTypeTable* THIS, RogueString* key_0 );
RogueClassTokenType* RogueString_TokenTypeTable__get( RogueClassString_TokenTypeTable* THIS, RogueString* key_0 );
RogueClassTokenType* RogueString_TokenTypeTable__remove( RogueClassString_TokenTypeTable* THIS, RogueString* key_0 );
void RogueString_TokenTypeTable__set( RogueClassString_TokenTypeTable* THIS, RogueString* key_0, RogueClassTokenType* value_1 );
RogueStringBuilder* RogueString_TokenTypeTable__print_to( RogueClassString_TokenTypeTable* THIS, RogueStringBuilder* buffer_0 );
RogueClassString_TokenTypeTable* RogueString_TokenTypeTable__init_object( RogueClassString_TokenTypeTable* THIS );
RogueString* RogueLiteralCharacterToken__to_String( RogueClassLiteralCharacterToken* THIS );
RogueString* RogueLiteralCharacterToken__type_name( RogueClassLiteralCharacterToken* THIS );
RogueCharacter RogueLiteralCharacterToken__to_Character( RogueClassLiteralCharacterToken* THIS );
RogueClassLiteralCharacterToken* RogueLiteralCharacterToken__init_object( RogueClassLiteralCharacterToken* THIS );
RogueClassLiteralCharacterToken* RogueLiteralCharacterToken__init( RogueClassLiteralCharacterToken* THIS, RogueClassTokenType* _auto_169_0, RogueCharacter _auto_170_1 );
RogueString* RogueLiteralIntegerToken__to_String( RogueClassLiteralIntegerToken* THIS );
RogueString* RogueLiteralIntegerToken__type_name( RogueClassLiteralIntegerToken* THIS );
RogueInteger RogueLiteralIntegerToken__to_Integer( RogueClassLiteralIntegerToken* THIS );
RogueReal RogueLiteralIntegerToken__to_Real( RogueClassLiteralIntegerToken* THIS );
RogueClassLiteralIntegerToken* RogueLiteralIntegerToken__init_object( RogueClassLiteralIntegerToken* THIS );
RogueClassLiteralIntegerToken* RogueLiteralIntegerToken__init( RogueClassLiteralIntegerToken* THIS, RogueClassTokenType* _auto_171_0, RogueInteger _auto_172_1 );
RogueString* RogueLiteralRealToken__to_String( RogueClassLiteralRealToken* THIS );
RogueString* RogueLiteralRealToken__type_name( RogueClassLiteralRealToken* THIS );
RogueInteger RogueLiteralRealToken__to_Integer( RogueClassLiteralRealToken* THIS );
RogueReal RogueLiteralRealToken__to_Real( RogueClassLiteralRealToken* THIS );
RogueClassLiteralRealToken* RogueLiteralRealToken__init_object( RogueClassLiteralRealToken* THIS );
RogueClassLiteralRealToken* RogueLiteralRealToken__init( RogueClassLiteralRealToken* THIS, RogueClassTokenType* _auto_173_0, RogueReal _auto_174_1 );
RogueString* RogueLiteralStringToken__to_String( RogueClassLiteralStringToken* THIS );
RogueString* RogueLiteralStringToken__type_name( RogueClassLiteralStringToken* THIS );
RogueClassLiteralStringToken* RogueLiteralStringToken__init_object( RogueClassLiteralStringToken* THIS );
RogueClassLiteralStringToken* RogueLiteralStringToken__init( RogueClassLiteralStringToken* THIS, RogueClassTokenType* _auto_175_0, RogueString* _auto_176_1 );
RogueString* RogueTypeIdentifierToken__to_String( RogueClassTypeIdentifierToken* THIS );
RogueString* RogueTypeIdentifierToken__type_name( RogueClassTypeIdentifierToken* THIS );
RogueClassType* RogueTypeIdentifierToken__to_Type( RogueClassTypeIdentifierToken* THIS );
RogueClassTypeIdentifierToken* RogueTypeIdentifierToken__init_object( RogueClassTypeIdentifierToken* THIS );
RogueClassTypeIdentifierToken* RogueTypeIdentifierToken__init( RogueClassTypeIdentifierToken* THIS, RogueClassTokenType* _auto_177_0, RogueClassType* _auto_178_1, RogueClassType* _auto_179_2 );
RogueString* RogueTypeParameter__type_name( RogueClassTypeParameter* THIS );
RogueClassTypeParameter* RogueTypeParameter__init( RogueClassTypeParameter* THIS, RogueClassToken* _auto_181_0, RogueString* _auto_182_1 );
RogueClassTypeParameter* RogueTypeParameter__init_object( RogueClassTypeParameter* THIS );
RogueString* RogueCmdLiteralInteger__type_name( RogueClassCmdLiteralInteger* THIS );
RogueClassCmd* RogueCmdLiteralInteger__cast_to( RogueClassCmdLiteralInteger* THIS, RogueClassType* target_type_0 );
RogueClassCmd* RogueCmdLiteralInteger__clone( RogueClassCmdLiteralInteger* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdLiteralInteger__write_cpp( RogueClassCmdLiteralInteger* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdLiteralInteger__resolve( RogueClassCmdLiteralInteger* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdLiteralInteger__type( RogueClassCmdLiteralInteger* THIS );
RogueClassCmdLiteralInteger* RogueCmdLiteralInteger__init_object( RogueClassCmdLiteralInteger* THIS );
RogueClassCmdLiteralInteger* RogueCmdLiteralInteger__init( RogueClassCmdLiteralInteger* THIS, RogueClassToken* _auto_183_0, RogueInteger _auto_184_1 );
RogueString* RogueCmdLiteral__type_name( RogueClassCmdLiteral* THIS );
RogueClassType* Rogue_CmdLiteral__implicit_type( RogueClassCmdLiteral* THIS );
RogueLogical RogueCmdLiteral__is_literal( RogueClassCmdLiteral* THIS );
RogueClassCmdLiteral* RogueCmdLiteral__init_object( RogueClassCmdLiteral* THIS );
RogueString* RogueCloneArgs__type_name( RogueClassCloneArgs* THIS );
RogueClassCloneArgs* RogueCloneArgs__init_object( RogueClassCloneArgs* THIS );
RogueString* RogueCmdArgs__type_name( RogueClassCmdArgs* THIS );
RogueClassCmdArgs* RogueCmdArgs__init_object( RogueClassCmdArgs* THIS );
RogueClassCmdArgs* RogueCmdArgs__init( RogueClassCmdArgs* THIS );
RogueClassCmdArgs* RogueCmdArgs__init( RogueClassCmdArgs* THIS, RogueInteger initial_capacity_0 );
RogueClassCmdArgs* RogueCmdArgs__init( RogueClassCmdArgs* THIS, RogueClassCmd* arg_0 );
RogueClassCmdArgs* RogueCmdArgs__init( RogueClassCmdArgs* THIS, RogueClassCmd* arg1_0, RogueClassCmd* arg2_1 );
RogueClassCmdArgs* RogueCmdArgs__clone( RogueClassCmdArgs* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdArgs__write_cpp( RogueClassCmdArgs* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
void RogueCmdArgs__resolve( RogueClassCmdArgs* THIS, RogueClassScope* scope_0 );
RogueString* RogueCmdAdd__type_name( RogueClassCmdAdd* THIS );
RogueClassCmd* RogueCmdAdd__clone( RogueClassCmdAdd* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdAdd__combine_literal_operands( RogueClassCmdAdd* THIS, RogueClassType* common_type_0 );
RogueClassCmdAdd* RogueCmdAdd__init_object( RogueClassCmdAdd* THIS );
RogueString* RogueCmdAdd__fn_name( RogueClassCmdAdd* THIS );
RogueClassCmd* RogueCmdAdd__resolve_operator_method( RogueClassCmdAdd* THIS, RogueClassScope* scope_0, RogueClassType* left_type_1, RogueClassType* right_type_2 );
RogueString* RogueCmdAdd__symbol( RogueClassCmdAdd* THIS );
RogueString* RogueCmdBinary__type_name( RogueClassCmdBinary* THIS );
void RogueCmdBinary__write_cpp( RogueClassCmdBinary* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdBinary__resolve( RogueClassCmdBinary* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdBinary__type( RogueClassCmdBinary* THIS );
RogueClassCmdBinary* RogueCmdBinary__init_object( RogueClassCmdBinary* THIS );
RogueClassCmdBinary* RogueCmdBinary__init( RogueClassCmdBinary* THIS, RogueClassToken* _auto_185_0, RogueClassCmd* _auto_186_1, RogueClassCmd* _auto_187_2 );
RogueString* RogueCmdBinary__cpp_symbol( RogueClassCmdBinary* THIS );
RogueString* RogueCmdBinary__fn_name( RogueClassCmdBinary* THIS );
RogueLogical RogueCmdBinary__requires_parens( RogueClassCmdBinary* THIS );
RogueClassCmd* RogueCmdBinary__resolve_for_types( RogueClassCmdBinary* THIS, RogueClassScope* scope_0, RogueClassType* left_type_1, RogueClassType* right_type_2 );
RogueClassCmd* Rogue_CmdBinary__resolve_for_common_type( RogueClassCmdBinary* THIS, RogueClassScope* scope_0, RogueClassType* common_type_1 );
RogueClassCmd* RogueCmdBinary__resolve_operator_method( RogueClassCmdBinary* THIS, RogueClassScope* scope_0, RogueClassType* left_type_1, RogueClassType* right_type_2 );
RogueString* RogueCmdBinary__symbol( RogueClassCmdBinary* THIS );
RogueString* RogueString_CmdTable__to_String( RogueClassString_CmdTable* THIS );
RogueString* RogueString_CmdTable__type_name( RogueClassString_CmdTable* THIS );
RogueClassString_CmdTable* RogueString_CmdTable__init( RogueClassString_CmdTable* THIS );
RogueClassString_CmdTable* RogueString_CmdTable__init( RogueClassString_CmdTable* THIS, RogueInteger bin_count_0 );
RogueClassString_CmdTable* RogueString_CmdTable__init( RogueClassString_CmdTable* THIS, RogueClassString_CmdTable* other_0 );
RogueClassString_CmdTable* RogueString_CmdTable__add( RogueClassString_CmdTable* THIS, RogueClassString_CmdTable* other_0 );
RogueClassCmd* RogueString_CmdTable__at( RogueClassString_CmdTable* THIS, RogueInteger index_0 );
void RogueString_CmdTable__clear( RogueClassString_CmdTable* THIS );
RogueClassString_CmdTable* RogueString_CmdTable__clone( RogueClassString_CmdTable* THIS );
RogueLogical RogueString_CmdTable__contains( RogueClassString_CmdTable* THIS, RogueString* key_0 );
RogueInteger RogueString_CmdTable__count( RogueClassString_CmdTable* THIS );
RogueClassString_CmdTableEntry* RogueString_CmdTable__find( RogueClassString_CmdTable* THIS, RogueString* key_0 );
RogueClassCmd* RogueString_CmdTable__get( RogueClassString_CmdTable* THIS, RogueString* key_0 );
RogueClassCmd* RogueString_CmdTable__remove( RogueClassString_CmdTable* THIS, RogueString* key_0 );
void RogueString_CmdTable__set( RogueClassString_CmdTable* THIS, RogueString* key_0, RogueClassCmd* value_1 );
RogueStringBuilder* RogueString_CmdTable__print_to( RogueClassString_CmdTable* THIS, RogueStringBuilder* buffer_0 );
RogueClassString_CmdTable* RogueString_CmdTable__init_object( RogueClassString_CmdTable* THIS );
RogueString* RoguePropertyArray__type_name( RogueArray* THIS );
RogueString* RogueCmdIf__type_name( RogueClassCmdIf* THIS );
RogueClassCmd* RogueCmdIf__clone( RogueClassCmdIf* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdIf__write_cpp( RogueClassCmdIf* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdIf__resolve( RogueClassCmdIf* THIS, RogueClassScope* scope_0 );
RogueClassCmdIf* RogueCmdIf__init_object( RogueClassCmdIf* THIS );
RogueClassCmdIf* RogueCmdIf__init( RogueClassCmdIf* THIS, RogueClassToken* _auto_195_0, RogueClassCmd* _auto_196_1, RogueInteger _auto_197_2 );
RogueClassCmdIf* RogueCmdIf__init( RogueClassCmdIf* THIS, RogueClassToken* _auto_198_0, RogueClassCmd* _auto_199_1, RogueClassCmdStatementList* _auto_200_2, RogueInteger _auto_201_3 );
RogueString* RogueCmdControlStructure__type_name( RogueClassCmdControlStructure* THIS );
RogueLogical RogueCmdControlStructure__requires_semicolon( RogueClassCmdControlStructure* THIS );
RogueClassCmdControlStructure* RogueCmdControlStructure__init_object( RogueClassCmdControlStructure* THIS );
RogueClassCmdControlStructure* RogueCmdControlStructure__init( RogueClassCmdControlStructure* THIS, RogueClassToken* _auto_194_0 );
RogueClassCmd* RogueCmdControlStructure__set_control_logic( RogueClassCmdControlStructure* THIS, RogueClassCmdControlStructure* control_structure_0 );
RogueString* RogueCmdWhich__type_name( RogueClassCmdWhich* THIS );
RogueClassCmdWhich* RogueCmdWhich__clone( RogueClassCmdWhich* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdWhich__resolve( RogueClassCmdWhich* THIS, RogueClassScope* scope_0 );
RogueClassCmdWhich* RogueCmdWhich__init_object( RogueClassCmdWhich* THIS );
RogueClassCmdWhich* RogueCmdWhich__init( RogueClassCmdWhich* THIS, RogueClassToken* _auto_202_0, RogueClassCmd* _auto_203_1, RogueCmdWhichCaseList* _auto_204_2, RogueClassCmdWhichCase* _auto_205_3, RogueInteger _auto_206_4 );
RogueClassCmdWhichCase* RogueCmdWhich__add_case( RogueClassCmdWhich* THIS, RogueClassToken* case_t_0 );
RogueClassCmdWhichCase* RogueCmdWhich__add_case_others( RogueClassCmdWhich* THIS, RogueClassToken* case_t_0 );
RogueString* RogueCmdContingent__type_name( RogueClassCmdContingent* THIS );
RogueClassCmd* RogueCmdContingent__clone( RogueClassCmdContingent* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdContingent__write_cpp( RogueClassCmdContingent* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmdContingent* RogueCmdContingent__resolve( RogueClassCmdContingent* THIS, RogueClassScope* scope_0 );
RogueClassCmdContingent* RogueCmdContingent__init_object( RogueClassCmdContingent* THIS );
RogueClassCmd* RogueCmdContingent__set_control_logic( RogueClassCmdContingent* THIS, RogueClassCmdControlStructure* original_0 );
RogueClassCmdContingent* RogueCmdContingent__init( RogueClassCmdContingent* THIS, RogueClassToken* _auto_207_0, RogueClassCmdStatementList* _auto_208_1 );
RogueString* RogueCmdGenericLoop__type_name( RogueClassCmdGenericLoop* THIS );
RogueClassCmd* RogueCmdGenericLoop__clone( RogueClassCmdGenericLoop* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdGenericLoop__write_cpp( RogueClassCmdGenericLoop* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdGenericLoop__resolve( RogueClassCmdGenericLoop* THIS, RogueClassScope* scope_0 );
RogueClassCmdGenericLoop* RogueCmdGenericLoop__init_object( RogueClassCmdGenericLoop* THIS );
RogueClassCmdGenericLoop* RogueCmdGenericLoop__init( RogueClassCmdGenericLoop* THIS, RogueClassToken* _auto_209_0, RogueInteger _auto_210_1, RogueClassCmd* _auto_211_2, RogueClassCmdStatementList* _auto_212_3, RogueClassCmdStatementList* _auto_213_4, RogueClassCmdStatementList* _auto_214_5 );
RogueClassCmdGenericLoop* RogueCmdGenericLoop__init( RogueClassCmdGenericLoop* THIS, RogueClassToken* _auto_215_0, RogueInteger _auto_216_1, RogueClassCmd* _auto_217_2, RogueClassCmdStatementList* _auto_218_3, RogueClassCmd* upkeep_cmd_4, RogueClassCmdStatementList* _auto_219_5 );
void RogueCmdGenericLoop__add_control_var( RogueClassCmdGenericLoop* THIS, RogueClassLocal* v_0 );
void RogueCmdGenericLoop__add_upkeep( RogueClassCmdGenericLoop* THIS, RogueClassCmd* cmd_0 );
RogueString* RogueCmdTry__type_name( RogueClassCmdTry* THIS );
RogueClassCmdTry* RogueCmdTry__clone( RogueClassCmdTry* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdTry__write_cpp( RogueClassCmdTry* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdTry__resolve( RogueClassCmdTry* THIS, RogueClassScope* scope_0 );
RogueClassCmdTry* RogueCmdTry__init_object( RogueClassCmdTry* THIS );
RogueClassCmdTry* RogueCmdTry__init( RogueClassCmdTry* THIS, RogueClassToken* _auto_220_0, RogueClassCmdStatementList* _auto_221_1, RogueCmdCatchList* _auto_222_2 );
RogueClassCmdCatch* RogueCmdTry__add_catch( RogueClassCmdTry* THIS, RogueClassToken* catch_t_0 );
RogueString* RogueCmdAwait__type_name( RogueClassCmdAwait* THIS );
RogueClassCmd* RogueCmdAwait__clone( RogueClassCmdAwait* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdAwait__resolve( RogueClassCmdAwait* THIS, RogueClassScope* scope_0 );
RogueClassCmdAwait* RogueCmdAwait__init_object( RogueClassCmdAwait* THIS );
RogueClassCmdAwait* RogueCmdAwait__init( RogueClassCmdAwait* THIS, RogueClassToken* _auto_223_0, RogueClassCmd* _auto_224_1, RogueClassCmdStatementList* _auto_225_2, RogueClassLocal* _auto_226_3 );
RogueString* RogueCmdYield__type_name( RogueClassCmdYield* THIS );
RogueClassCmd* RogueCmdYield__clone( RogueClassCmdYield* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdYield__resolve( RogueClassCmdYield* THIS, RogueClassScope* scope_0 );
RogueClassCmdYield* RogueCmdYield__init_object( RogueClassCmdYield* THIS );
RogueClassCmdYield* RogueCmdYield__init( RogueClassCmdYield* THIS, RogueClassToken* _auto_227_0 );
RogueString* RogueCmdThrow__type_name( RogueClassCmdThrow* THIS );
RogueClassCmdThrow* RogueCmdThrow__clone( RogueClassCmdThrow* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdThrow__write_cpp( RogueClassCmdThrow* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdThrow__resolve( RogueClassCmdThrow* THIS, RogueClassScope* scope_0 );
RogueClassCmdThrow* RogueCmdThrow__init_object( RogueClassCmdThrow* THIS );
RogueClassCmdThrow* RogueCmdThrow__init( RogueClassCmdThrow* THIS, RogueClassToken* _auto_228_0, RogueClassCmd* _auto_229_1 );
RogueString* RogueCmdTrace__type_name( RogueClassCmdTrace* THIS );
RogueClassCmdTrace* RogueCmdTrace__clone( RogueClassCmdTrace* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdTrace__resolve( RogueClassCmdTrace* THIS, RogueClassScope* scope_0 );
RogueClassCmdTrace* RogueCmdTrace__init_object( RogueClassCmdTrace* THIS );
RogueClassCmdTrace* RogueCmdTrace__init( RogueClassCmdTrace* THIS, RogueClassToken* _auto_230_0, RogueString* _auto_231_1 );
RogueString* RogueCmdEscape__type_name( RogueClassCmdEscape* THIS );
RogueClassCmd* RogueCmdEscape__clone( RogueClassCmdEscape* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdEscape__write_cpp( RogueClassCmdEscape* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdEscape__resolve( RogueClassCmdEscape* THIS, RogueClassScope* scope_0 );
RogueClassCmdEscape* RogueCmdEscape__init_object( RogueClassCmdEscape* THIS );
RogueClassCmdEscape* RogueCmdEscape__init( RogueClassCmdEscape* THIS, RogueClassToken* _auto_232_0, RogueInteger _auto_233_1, RogueClassCmdControlStructure* _auto_234_2 );
RogueString* RogueCmdNextIteration__type_name( RogueClassCmdNextIteration* THIS );
RogueClassCmd* RogueCmdNextIteration__clone( RogueClassCmdNextIteration* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdNextIteration__write_cpp( RogueClassCmdNextIteration* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdNextIteration__resolve( RogueClassCmdNextIteration* THIS, RogueClassScope* scope_0 );
RogueClassCmdNextIteration* RogueCmdNextIteration__init_object( RogueClassCmdNextIteration* THIS );
RogueClassCmdNextIteration* RogueCmdNextIteration__init( RogueClassCmdNextIteration* THIS, RogueClassToken* _auto_235_0, RogueClassCmdControlStructure* _auto_236_1 );
RogueString* RogueCmdNecessary__type_name( RogueClassCmdNecessary* THIS );
RogueClassCmd* RogueCmdNecessary__clone( RogueClassCmdNecessary* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdNecessary__write_cpp( RogueClassCmdNecessary* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdNecessary__resolve( RogueClassCmdNecessary* THIS, RogueClassScope* scope_0 );
RogueClassCmdNecessary* RogueCmdNecessary__init_object( RogueClassCmdNecessary* THIS );
RogueClassCmdNecessary* RogueCmdNecessary__init( RogueClassCmdNecessary* THIS, RogueClassToken* _auto_237_0, RogueClassCmd* _auto_238_1, RogueClassCmdContingent* _auto_239_2 );
RogueString* RogueCmdSufficient__type_name( RogueClassCmdSufficient* THIS );
RogueClassCmd* RogueCmdSufficient__clone( RogueClassCmdSufficient* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdSufficient__write_cpp( RogueClassCmdSufficient* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdSufficient__resolve( RogueClassCmdSufficient* THIS, RogueClassScope* scope_0 );
RogueClassCmdSufficient* RogueCmdSufficient__init_object( RogueClassCmdSufficient* THIS );
RogueClassCmdSufficient* RogueCmdSufficient__init( RogueClassCmdSufficient* THIS, RogueClassToken* _auto_240_0, RogueClassCmd* _auto_241_1, RogueClassCmdContingent* _auto_242_2 );
RogueString* RogueCmdAdjust__type_name( RogueClassCmdAdjust* THIS );
RogueClassCmd* RogueCmdAdjust__resolve( RogueClassCmdAdjust* THIS, RogueClassScope* scope_0 );
RogueClassCmdAdjust* RogueCmdAdjust__init_object( RogueClassCmdAdjust* THIS );
RogueClassCmdAdjust* RogueCmdAdjust__init( RogueClassCmdAdjust* THIS, RogueClassToken* _auto_243_0, RogueClassCmd* _auto_244_1, RogueInteger _auto_245_2 );
RogueString* RogueCmdAssign__type_name( RogueClassCmdAssign* THIS );
RogueClassCmd* RogueCmdAssign__clone( RogueClassCmdAssign* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdAssign__write_cpp( RogueClassCmdAssign* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdAssign__resolve( RogueClassCmdAssign* THIS, RogueClassScope* scope_0 );
RogueClassCmdAssign* RogueCmdAssign__init_object( RogueClassCmdAssign* THIS );
RogueClassCmdAssign* RogueCmdAssign__init( RogueClassCmdAssign* THIS, RogueClassToken* _auto_246_0, RogueClassCmd* _auto_247_1, RogueClassCmd* _auto_248_2 );
RogueString* RogueCmdOpWithAssign__type_name( RogueClassCmdOpWithAssign* THIS );
RogueClassCmd* RogueCmdOpWithAssign__clone( RogueClassCmdOpWithAssign* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdOpWithAssign__resolve( RogueClassCmdOpWithAssign* THIS, RogueClassScope* scope_0 );
RogueClassCmdOpWithAssign* RogueCmdOpWithAssign__init_object( RogueClassCmdOpWithAssign* THIS );
RogueClassCmdOpWithAssign* RogueCmdOpWithAssign__init( RogueClassCmdOpWithAssign* THIS, RogueClassToken* _auto_249_0, RogueClassCmd* _auto_250_1, RogueClassTokenType* _auto_251_2, RogueClassCmd* _auto_252_3 );
RogueString* RogueCmdAccess__type_name( RogueClassCmdAccess* THIS );
RogueClassCmd* RogueCmdAccess__clone( RogueClassCmdAccess* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassType* Rogue_CmdAccess__implicit_type( RogueClassCmdAccess* THIS );
RogueClassCmd* RogueCmdAccess__resolve( RogueClassCmdAccess* THIS, RogueClassScope* scope_0 );
RogueClassCmd* RogueCmdAccess__resolve_assignment( RogueClassCmdAccess* THIS, RogueClassScope* scope_0, RogueClassCmd* new_value_1 );
RogueClassCmd* RogueCmdAccess__resolve_modify_and_assign( RogueClassCmdAccess* THIS, RogueClassScope* scope_0, RogueClassTokenType* op_1, RogueClassCmd* new_value_2 );
RogueClassType* Rogue_CmdAccess__type( RogueClassCmdAccess* THIS );
RogueClassCmdAccess* RogueCmdAccess__init_object( RogueClassCmdAccess* THIS );
RogueClassCmdAccess* RogueCmdAccess__init( RogueClassCmdAccess* THIS, RogueClassToken* _auto_253_0, RogueString* _auto_254_1 );
RogueClassCmdAccess* RogueCmdAccess__init( RogueClassCmdAccess* THIS, RogueClassToken* _auto_255_0, RogueString* _auto_256_1, RogueClassCmdArgs* _auto_257_2 );
RogueClassCmdAccess* RogueCmdAccess__init( RogueClassCmdAccess* THIS, RogueClassToken* _auto_258_0, RogueClassCmd* _auto_259_1, RogueString* _auto_260_2 );
RogueClassCmdAccess* RogueCmdAccess__init( RogueClassCmdAccess* THIS, RogueClassToken* _auto_261_0, RogueClassCmd* _auto_262_1, RogueString* _auto_263_2, RogueClassCmdArgs* _auto_264_3 );
RogueClassCmdAccess* RogueCmdAccess__init( RogueClassCmdAccess* THIS, RogueClassToken* _auto_265_0, RogueClassCmd* _auto_266_1, RogueString* _auto_267_2, RogueClassCmd* arg_3 );
void RogueCmdAccess__check_for_recursive_getter( RogueClassCmdAccess* THIS, RogueClassScope* scope_0 );
RogueString* RogueCmdWhichCaseList__to_String( RogueCmdWhichCaseList* THIS );
RogueString* RogueCmdWhichCaseList__type_name( RogueCmdWhichCaseList* THIS );
RogueCmdWhichCaseList* RogueCmdWhichCaseList__init_object( RogueCmdWhichCaseList* THIS );
RogueCmdWhichCaseList* RogueCmdWhichCaseList__init( RogueCmdWhichCaseList* THIS );
RogueCmdWhichCaseList* RogueCmdWhichCaseList__init( RogueCmdWhichCaseList* THIS, RogueInteger initial_capacity_0 );
RogueCmdWhichCaseList* RogueCmdWhichCaseList__init( RogueCmdWhichCaseList* THIS, RogueInteger initial_capacity_0, RogueClassCmdWhichCase* initial_value_1 );
RogueCmdWhichCaseList* RogueCmdWhichCaseList__clone( RogueCmdWhichCaseList* THIS );
RogueCmdWhichCaseList* RogueCmdWhichCaseList__add( RogueCmdWhichCaseList* THIS, RogueClassCmdWhichCase* value_0 );
RogueCmdWhichCaseList* RogueCmdWhichCaseList__add( RogueCmdWhichCaseList* THIS, RogueCmdWhichCaseList* other_0 );
RogueInteger RogueCmdWhichCaseList__capacity( RogueCmdWhichCaseList* THIS );
RogueCmdWhichCaseList* RogueCmdWhichCaseList__clear( RogueCmdWhichCaseList* THIS );
RogueCmdWhichCaseList* RogueCmdWhichCaseList__insert( RogueCmdWhichCaseList* THIS, RogueClassCmdWhichCase* value_0, RogueInteger before_index_1 );
RogueClassCmdWhichCase* RogueCmdWhichCaseList__last( RogueCmdWhichCaseList* THIS );
RogueClassCmdWhichCase* RogueCmdWhichCaseList__random( RogueCmdWhichCaseList* THIS );
RogueCmdWhichCaseList* RogueCmdWhichCaseList__reserve( RogueCmdWhichCaseList* THIS, RogueInteger additional_count_0 );
RogueClassCmdWhichCase* RogueCmdWhichCaseList__remove( RogueCmdWhichCaseList* THIS, RogueClassCmdWhichCase* value_0 );
RogueClassCmdWhichCase* RogueCmdWhichCaseList__remove_at( RogueCmdWhichCaseList* THIS, RogueInteger index_0 );
RogueClassCmdWhichCase* RogueCmdWhichCaseList__remove_last( RogueCmdWhichCaseList* THIS );
RogueString* RogueCmdWhichCase__type_name( RogueClassCmdWhichCase* THIS );
RogueClassCmdWhichCase* RogueCmdWhichCase__clone( RogueClassCmdWhichCase* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmdWhichCase* RogueCmdWhichCase__init_object( RogueClassCmdWhichCase* THIS );
RogueClassCmdWhichCase* RogueCmdWhichCase__init( RogueClassCmdWhichCase* THIS, RogueClassToken* _auto_268_0, RogueClassCmdArgs* _auto_269_1, RogueClassCmdStatementList* _auto_270_2 );
RogueClassCmd* RogueCmdWhichCase__as_conditional( RogueClassCmdWhichCase* THIS, RogueString* expression_var_name_0 );
RogueString* RogueCmdCatchList__to_String( RogueCmdCatchList* THIS );
RogueString* RogueCmdCatchList__type_name( RogueCmdCatchList* THIS );
RogueCmdCatchList* RogueCmdCatchList__init_object( RogueCmdCatchList* THIS );
RogueCmdCatchList* RogueCmdCatchList__init( RogueCmdCatchList* THIS );
RogueCmdCatchList* RogueCmdCatchList__init( RogueCmdCatchList* THIS, RogueInteger initial_capacity_0 );
RogueCmdCatchList* RogueCmdCatchList__init( RogueCmdCatchList* THIS, RogueInteger initial_capacity_0, RogueClassCmdCatch* initial_value_1 );
RogueCmdCatchList* RogueCmdCatchList__clone( RogueCmdCatchList* THIS );
RogueCmdCatchList* RogueCmdCatchList__add( RogueCmdCatchList* THIS, RogueClassCmdCatch* value_0 );
RogueCmdCatchList* RogueCmdCatchList__add( RogueCmdCatchList* THIS, RogueCmdCatchList* other_0 );
RogueInteger RogueCmdCatchList__capacity( RogueCmdCatchList* THIS );
RogueCmdCatchList* RogueCmdCatchList__clear( RogueCmdCatchList* THIS );
RogueCmdCatchList* RogueCmdCatchList__insert( RogueCmdCatchList* THIS, RogueClassCmdCatch* value_0, RogueInteger before_index_1 );
RogueClassCmdCatch* RogueCmdCatchList__last( RogueCmdCatchList* THIS );
RogueClassCmdCatch* RogueCmdCatchList__random( RogueCmdCatchList* THIS );
RogueCmdCatchList* RogueCmdCatchList__reserve( RogueCmdCatchList* THIS, RogueInteger additional_count_0 );
RogueClassCmdCatch* RogueCmdCatchList__remove( RogueCmdCatchList* THIS, RogueClassCmdCatch* value_0 );
RogueClassCmdCatch* RogueCmdCatchList__remove_at( RogueCmdCatchList* THIS, RogueInteger index_0 );
RogueClassCmdCatch* RogueCmdCatchList__remove_last( RogueCmdCatchList* THIS );
RogueString* RogueCmdCatch__type_name( RogueClassCmdCatch* THIS );
RogueClassCmdCatch* RogueCmdCatch__clone( RogueClassCmdCatch* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdCatch__write_cpp( RogueClassCmdCatch* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdCatch__resolve( RogueClassCmdCatch* THIS, RogueClassScope* scope_0 );
RogueClassCmdCatch* RogueCmdCatch__init_object( RogueClassCmdCatch* THIS );
RogueClassCmdCatch* RogueCmdCatch__init( RogueClassCmdCatch* THIS, RogueClassToken* _auto_272_0, RogueClassLocal* _auto_273_1, RogueClassCmdStatementList* _auto_274_2 );
RogueString* RogueCmdLocalDeclaration__type_name( RogueClassCmdLocalDeclaration* THIS );
RogueClassCmd* RogueCmdLocalDeclaration__clone( RogueClassCmdLocalDeclaration* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdLocalDeclaration__exit_scope( RogueClassCmdLocalDeclaration* THIS, RogueClassScope* scope_0 );
void RogueCmdLocalDeclaration__write_cpp( RogueClassCmdLocalDeclaration* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdLocalDeclaration__resolve( RogueClassCmdLocalDeclaration* THIS, RogueClassScope* scope_0 );
RogueClassCmdLocalDeclaration* RogueCmdLocalDeclaration__init_object( RogueClassCmdLocalDeclaration* THIS );
RogueClassCmdLocalDeclaration* RogueCmdLocalDeclaration__init( RogueClassCmdLocalDeclaration* THIS, RogueClassToken* _auto_282_0, RogueClassLocal* _auto_283_1 );
RogueString* RogueCmdAdjustLocal__type_name( RogueClassCmdAdjustLocal* THIS );
RogueClassCmd* RogueCmdAdjustLocal__clone( RogueClassCmdAdjustLocal* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdAdjustLocal__write_cpp( RogueClassCmdAdjustLocal* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdAdjustLocal__resolve( RogueClassCmdAdjustLocal* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdAdjustLocal__type( RogueClassCmdAdjustLocal* THIS );
RogueClassCmdAdjustLocal* RogueCmdAdjustLocal__init_object( RogueClassCmdAdjustLocal* THIS );
RogueClassCmdAdjustLocal* RogueCmdAdjustLocal__init( RogueClassCmdAdjustLocal* THIS, RogueClassToken* _auto_288_0, RogueClassLocal* _auto_289_1, RogueInteger _auto_290_2 );
RogueString* RogueCmdReadLocal__type_name( RogueClassCmdReadLocal* THIS );
RogueString* RogueCmdReadLocal__access_name( RogueClassCmdReadLocal* THIS );
RogueString* RogueCmdReadLocal__access_cpp_name( RogueClassCmdReadLocal* THIS );
RogueClassCmd* RogueCmdReadLocal__clone( RogueClassCmdReadLocal* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdReadLocal__write_cpp( RogueClassCmdReadLocal* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdReadLocal__resolve( RogueClassCmdReadLocal* THIS, RogueClassScope* scope_0 );
RogueClassCmd* RogueCmdReadLocal__resolve_modify( RogueClassCmdReadLocal* THIS, RogueClassScope* scope_0, RogueInteger delta_1 );
RogueClassType* Rogue_CmdReadLocal__type( RogueClassCmdReadLocal* THIS );
RogueClassCmdReadLocal* RogueCmdReadLocal__init_object( RogueClassCmdReadLocal* THIS );
RogueClassCmdReadLocal* RogueCmdReadLocal__init( RogueClassCmdReadLocal* THIS, RogueClassToken* _auto_291_0, RogueClassLocal* _auto_292_1 );
RogueString* RogueCmdCompareLE__type_name( RogueClassCmdCompareLE* THIS );
RogueClassCmd* RogueCmdCompareLE__clone( RogueClassCmdCompareLE* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdCompareLE__combine_literal_operands( RogueClassCmdCompareLE* THIS, RogueClassType* common_type_0 );
RogueClassCmdCompareLE* RogueCmdCompareLE__init_object( RogueClassCmdCompareLE* THIS );
RogueString* RogueCmdCompareLE__symbol( RogueClassCmdCompareLE* THIS );
RogueClassCmd* RogueCmdCompareLE__resolve_for_reference( RogueClassCmdCompareLE* THIS, RogueClassScope* scope_0, RogueClassType* left_type_1, RogueClassType* right_type_2, RogueLogical force_error_3 );
RogueString* RogueCmdComparison__type_name( RogueClassCmdComparison* THIS );
RogueClassType* Rogue_CmdComparison__type( RogueClassCmdComparison* THIS );
RogueClassCmdComparison* RogueCmdComparison__init_object( RogueClassCmdComparison* THIS );
RogueLogical RogueCmdComparison__requires_parens( RogueClassCmdComparison* THIS );
RogueClassCmd* RogueCmdComparison__resolve_for_types( RogueClassCmdComparison* THIS, RogueClassScope* scope_0, RogueClassType* left_type_1, RogueClassType* right_type_2 );
RogueClassCmd* RogueCmdComparison__resolve_for_reference( RogueClassCmdComparison* THIS, RogueClassScope* scope_0, RogueClassType* left_type_1, RogueClassType* right_type_2, RogueLogical force_error_3 );
RogueString* RogueCmdRange__type_name( RogueClassCmdRange* THIS );
RogueClassCmdRange* RogueCmdRange__init_object( RogueClassCmdRange* THIS );
RogueClassCmdRange* RogueCmdRange__init( RogueClassCmdRange* THIS, RogueClassToken* _auto_293_0, RogueClassCmd* _auto_294_1, RogueClassCmd* _auto_295_2, RogueClassCmd* _auto_296_3 );
RogueString* RogueCmdLocalOpWithAssign__type_name( RogueClassCmdLocalOpWithAssign* THIS );
RogueClassCmd* RogueCmdLocalOpWithAssign__clone( RogueClassCmdLocalOpWithAssign* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdLocalOpWithAssign__write_cpp( RogueClassCmdLocalOpWithAssign* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdLocalOpWithAssign__resolve( RogueClassCmdLocalOpWithAssign* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdLocalOpWithAssign__type( RogueClassCmdLocalOpWithAssign* THIS );
RogueClassCmdLocalOpWithAssign* RogueCmdLocalOpWithAssign__init_object( RogueClassCmdLocalOpWithAssign* THIS );
RogueClassCmdLocalOpWithAssign* RogueCmdLocalOpWithAssign__init( RogueClassCmdLocalOpWithAssign* THIS, RogueClassToken* _auto_297_0, RogueClassLocal* _auto_298_1, RogueClassTokenType* _auto_299_2, RogueClassCmd* _auto_300_3 );
RogueString* RogueCmdResolvedOpWithAssign__type_name( RogueClassCmdResolvedOpWithAssign* THIS );
RogueClassCmdResolvedOpWithAssign* RogueCmdResolvedOpWithAssign__init_object( RogueClassCmdResolvedOpWithAssign* THIS );
RogueString* RogueCmdResolvedOpWithAssign__cpp_symbol( RogueClassCmdResolvedOpWithAssign* THIS );
RogueString* RogueCmdResolvedOpWithAssign__symbol( RogueClassCmdResolvedOpWithAssign* THIS );
RogueString* RogueCmdForEach__type_name( RogueClassCmdForEach* THIS );
RogueClassCmd* RogueCmdForEach__clone( RogueClassCmdForEach* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdForEach__resolve( RogueClassCmdForEach* THIS, RogueClassScope* scope_0 );
RogueClassCmdForEach* RogueCmdForEach__init_object( RogueClassCmdForEach* THIS );
RogueClassCmdForEach* RogueCmdForEach__init( RogueClassCmdForEach* THIS, RogueClassToken* _auto_301_0, RogueString* _auto_302_1, RogueString* _auto_303_2, RogueClassCmd* _auto_304_3, RogueClassCmd* _auto_305_4, RogueClassCmdStatementList* _auto_306_5 );
RogueString* RogueCmdRangeUpTo__type_name( RogueClassCmdRangeUpTo* THIS );
RogueClassCmd* RogueCmdRangeUpTo__clone( RogueClassCmdRangeUpTo* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmdRangeUpTo* RogueCmdRangeUpTo__init_object( RogueClassCmdRangeUpTo* THIS );
RogueString* RogueCmdLogicalXor__type_name( RogueClassCmdLogicalXor* THIS );
RogueClassCmd* RogueCmdLogicalXor__clone( RogueClassCmdLogicalXor* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmdLogicalXor* RogueCmdLogicalXor__init_object( RogueClassCmdLogicalXor* THIS );
RogueString* RogueCmdLogicalXor__cpp_symbol( RogueClassCmdLogicalXor* THIS );
RogueString* RogueCmdLogicalXor__symbol( RogueClassCmdLogicalXor* THIS );
RogueLogical RogueCmdLogicalXor__combine_literal_operands( RogueClassCmdLogicalXor* THIS, RogueLogical a_0, RogueLogical b_1 );
RogueString* RogueCmdBinaryLogical__type_name( RogueClassCmdBinaryLogical* THIS );
RogueClassCmd* RogueCmdBinaryLogical__resolve( RogueClassCmdBinaryLogical* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdBinaryLogical__type( RogueClassCmdBinaryLogical* THIS );
RogueClassCmdBinaryLogical* RogueCmdBinaryLogical__init_object( RogueClassCmdBinaryLogical* THIS );
RogueClassCmd* RogueCmdBinaryLogical__resolve_operator_method( RogueClassCmdBinaryLogical* THIS, RogueClassScope* scope_0, RogueClassType* left_type_1, RogueClassType* right_type_2 );
RogueLogical RogueCmdBinaryLogical__combine_literal_operands( RogueClassCmdBinaryLogical* THIS, RogueLogical a_0, RogueLogical b_1 );
RogueString* RogueCmdLogicalOr__type_name( RogueClassCmdLogicalOr* THIS );
RogueClassCmd* RogueCmdLogicalOr__clone( RogueClassCmdLogicalOr* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmdLogicalOr* RogueCmdLogicalOr__init_object( RogueClassCmdLogicalOr* THIS );
RogueString* RogueCmdLogicalOr__cpp_symbol( RogueClassCmdLogicalOr* THIS );
RogueString* RogueCmdLogicalOr__symbol( RogueClassCmdLogicalOr* THIS );
RogueLogical RogueCmdLogicalOr__combine_literal_operands( RogueClassCmdLogicalOr* THIS, RogueLogical a_0, RogueLogical b_1 );
RogueString* RogueCmdLogicalAnd__type_name( RogueClassCmdLogicalAnd* THIS );
RogueClassCmd* RogueCmdLogicalAnd__clone( RogueClassCmdLogicalAnd* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmdLogicalAnd* RogueCmdLogicalAnd__init_object( RogueClassCmdLogicalAnd* THIS );
RogueString* RogueCmdLogicalAnd__cpp_symbol( RogueClassCmdLogicalAnd* THIS );
RogueString* RogueCmdLogicalAnd__symbol( RogueClassCmdLogicalAnd* THIS );
RogueLogical RogueCmdLogicalAnd__combine_literal_operands( RogueClassCmdLogicalAnd* THIS, RogueLogical a_0, RogueLogical b_1 );
RogueString* RogueCmdCompareEQ__type_name( RogueClassCmdCompareEQ* THIS );
RogueClassCmd* RogueCmdCompareEQ__clone( RogueClassCmdCompareEQ* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdCompareEQ__combine_literal_operands( RogueClassCmdCompareEQ* THIS, RogueClassType* common_type_0 );
RogueClassCmdCompareEQ* RogueCmdCompareEQ__init_object( RogueClassCmdCompareEQ* THIS );
RogueLogical RogueCmdCompareEQ__requires_parens( RogueClassCmdCompareEQ* THIS );
RogueString* RogueCmdCompareEQ__symbol( RogueClassCmdCompareEQ* THIS );
RogueClassCmd* RogueCmdCompareEQ__resolve_for_reference( RogueClassCmdCompareEQ* THIS, RogueClassScope* scope_0, RogueClassType* left_type_1, RogueClassType* right_type_2, RogueLogical force_error_3 );
RogueString* RogueCmdCompareIs__type_name( RogueClassCmdCompareIs* THIS );
RogueClassCmd* RogueCmdCompareIs__clone( RogueClassCmdCompareIs* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmdCompareIs* RogueCmdCompareIs__init_object( RogueClassCmdCompareIs* THIS );
RogueString* RogueCmdCompareIs__cpp_symbol( RogueClassCmdCompareIs* THIS );
RogueClassCmd* RogueCmdCompareIs__resolve_for_types( RogueClassCmdCompareIs* THIS, RogueClassScope* scope_0, RogueClassType* left_type_1, RogueClassType* right_type_2 );
RogueString* RogueCmdCompareIs__symbol( RogueClassCmdCompareIs* THIS );
RogueString* RogueCmdCompareNE__type_name( RogueClassCmdCompareNE* THIS );
RogueClassCmd* RogueCmdCompareNE__clone( RogueClassCmdCompareNE* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdCompareNE__combine_literal_operands( RogueClassCmdCompareNE* THIS, RogueClassType* common_type_0 );
RogueClassCmdCompareNE* RogueCmdCompareNE__init_object( RogueClassCmdCompareNE* THIS );
RogueString* RogueCmdCompareNE__symbol( RogueClassCmdCompareNE* THIS );
RogueClassCmd* RogueCmdCompareNE__resolve_for_reference( RogueClassCmdCompareNE* THIS, RogueClassScope* scope_0, RogueClassType* left_type_1, RogueClassType* right_type_2, RogueLogical force_error_3 );
RogueString* RogueCmdCompareIsNot__type_name( RogueClassCmdCompareIsNot* THIS );
RogueClassCmd* RogueCmdCompareIsNot__clone( RogueClassCmdCompareIsNot* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmdCompareIsNot* RogueCmdCompareIsNot__init_object( RogueClassCmdCompareIsNot* THIS );
RogueString* RogueCmdCompareIsNot__cpp_symbol( RogueClassCmdCompareIsNot* THIS );
RogueClassCmd* RogueCmdCompareIsNot__resolve_for_types( RogueClassCmdCompareIsNot* THIS, RogueClassScope* scope_0, RogueClassType* left_type_1, RogueClassType* right_type_2 );
RogueString* RogueCmdCompareIsNot__symbol( RogueClassCmdCompareIsNot* THIS );
RogueString* RogueCmdCompareLT__type_name( RogueClassCmdCompareLT* THIS );
RogueClassCmd* RogueCmdCompareLT__clone( RogueClassCmdCompareLT* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdCompareLT__combine_literal_operands( RogueClassCmdCompareLT* THIS, RogueClassType* common_type_0 );
RogueClassCmdCompareLT* RogueCmdCompareLT__init_object( RogueClassCmdCompareLT* THIS );
RogueString* RogueCmdCompareLT__symbol( RogueClassCmdCompareLT* THIS );
RogueClassCmd* RogueCmdCompareLT__resolve_for_reference( RogueClassCmdCompareLT* THIS, RogueClassScope* scope_0, RogueClassType* left_type_1, RogueClassType* right_type_2, RogueLogical force_error_3 );
RogueString* RogueCmdCompareGT__type_name( RogueClassCmdCompareGT* THIS );
RogueClassCmd* RogueCmdCompareGT__clone( RogueClassCmdCompareGT* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdCompareGT__combine_literal_operands( RogueClassCmdCompareGT* THIS, RogueClassType* common_type_0 );
RogueClassCmdCompareGT* RogueCmdCompareGT__init_object( RogueClassCmdCompareGT* THIS );
RogueString* RogueCmdCompareGT__symbol( RogueClassCmdCompareGT* THIS );
RogueClassCmd* RogueCmdCompareGT__resolve_for_reference( RogueClassCmdCompareGT* THIS, RogueClassScope* scope_0, RogueClassType* left_type_1, RogueClassType* right_type_2, RogueLogical force_error_3 );
RogueString* RogueCmdCompareGE__type_name( RogueClassCmdCompareGE* THIS );
RogueClassCmd* RogueCmdCompareGE__clone( RogueClassCmdCompareGE* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdCompareGE__combine_literal_operands( RogueClassCmdCompareGE* THIS, RogueClassType* common_type_0 );
RogueClassCmdCompareGE* RogueCmdCompareGE__init_object( RogueClassCmdCompareGE* THIS );
RogueString* RogueCmdCompareGE__symbol( RogueClassCmdCompareGE* THIS );
RogueClassCmd* RogueCmdCompareGE__resolve_for_reference( RogueClassCmdCompareGE* THIS, RogueClassScope* scope_0, RogueClassType* left_type_1, RogueClassType* right_type_2, RogueLogical force_error_3 );
RogueString* RogueCmdInstanceOf__type_name( RogueClassCmdInstanceOf* THIS );
RogueClassCmd* RogueCmdInstanceOf__clone( RogueClassCmdInstanceOf* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdInstanceOf__write_cpp( RogueClassCmdInstanceOf* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdInstanceOf__resolve( RogueClassCmdInstanceOf* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdInstanceOf__type( RogueClassCmdInstanceOf* THIS );
RogueClassCmdInstanceOf* RogueCmdInstanceOf__init_object( RogueClassCmdInstanceOf* THIS );
RogueString* RogueCmdTypeOperator__type_name( RogueClassCmdTypeOperator* THIS );
RogueClassType* Rogue_CmdTypeOperator__type( RogueClassCmdTypeOperator* THIS );
RogueClassCmdTypeOperator* RogueCmdTypeOperator__init_object( RogueClassCmdTypeOperator* THIS );
RogueClassCmdTypeOperator* RogueCmdTypeOperator__init( RogueClassCmdTypeOperator* THIS, RogueClassToken* _auto_307_0, RogueClassCmd* _auto_308_1, RogueClassType* _auto_309_2 );
RogueString* RogueCmdLogicalNot__type_name( RogueClassCmdLogicalNot* THIS );
RogueClassCmd* RogueCmdLogicalNot__clone( RogueClassCmdLogicalNot* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdLogicalNot__resolve( RogueClassCmdLogicalNot* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdLogicalNot__type( RogueClassCmdLogicalNot* THIS );
RogueClassCmdLogicalNot* RogueCmdLogicalNot__init_object( RogueClassCmdLogicalNot* THIS );
RogueString* RogueCmdLogicalNot__cpp_prefix_symbol( RogueClassCmdLogicalNot* THIS );
RogueString* RogueCmdLogicalNot__prefix_symbol( RogueClassCmdLogicalNot* THIS );
RogueClassCmd* RogueCmdLogicalNot__resolve_for_literal_operand( RogueClassCmdLogicalNot* THIS, RogueClassScope* scope_0 );
RogueString* RogueCmdUnary__type_name( RogueClassCmdUnary* THIS );
void RogueCmdUnary__write_cpp( RogueClassCmdUnary* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdUnary__resolve( RogueClassCmdUnary* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdUnary__type( RogueClassCmdUnary* THIS );
RogueClassCmdUnary* RogueCmdUnary__init_object( RogueClassCmdUnary* THIS );
RogueClassCmdUnary* RogueCmdUnary__init( RogueClassCmdUnary* THIS, RogueClassToken* _auto_310_0, RogueClassCmd* _auto_311_1 );
RogueString* RogueCmdUnary__cpp_prefix_symbol( RogueClassCmdUnary* THIS );
RogueString* RogueCmdUnary__cpp_suffix_symbol( RogueClassCmdUnary* THIS );
RogueString* RogueCmdUnary__prefix_symbol( RogueClassCmdUnary* THIS );
RogueClassCmd* RogueCmdUnary__resolve_for_literal_operand( RogueClassCmdUnary* THIS, RogueClassScope* scope_0 );
RogueClassCmd* Rogue_CmdUnary__resolve_for_operand_type( RogueClassCmdUnary* THIS, RogueClassScope* scope_0, RogueClassType* operand_type_1 );
RogueString* RogueCmdUnary__suffix_symbol( RogueClassCmdUnary* THIS );
RogueString* RogueCmdBitwiseXor__type_name( RogueClassCmdBitwiseXor* THIS );
RogueClassCmd* RogueCmdBitwiseXor__clone( RogueClassCmdBitwiseXor* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdBitwiseXor__combine_literal_operands( RogueClassCmdBitwiseXor* THIS, RogueClassType* common_type_0 );
RogueClassCmdBitwiseXor* RogueCmdBitwiseXor__init_object( RogueClassCmdBitwiseXor* THIS );
RogueString* RogueCmdBitwiseXor__cpp_symbol( RogueClassCmdBitwiseXor* THIS );
RogueString* RogueCmdBitwiseXor__symbol( RogueClassCmdBitwiseXor* THIS );
RogueString* RogueCmdBitwiseOp__type_name( RogueClassCmdBitwiseOp* THIS );
RogueClassCmdBitwiseOp* RogueCmdBitwiseOp__init_object( RogueClassCmdBitwiseOp* THIS );
RogueClassCmd* Rogue_CmdBitwiseOp__resolve_for_common_type( RogueClassCmdBitwiseOp* THIS, RogueClassScope* scope_0, RogueClassType* common_type_1 );
RogueClassCmd* RogueCmdBitwiseOp__resolve_operator_method( RogueClassCmdBitwiseOp* THIS, RogueClassScope* scope_0, RogueClassType* left_type_1, RogueClassType* right_type_2 );
RogueString* RogueCmdBitwiseOr__type_name( RogueClassCmdBitwiseOr* THIS );
RogueClassCmd* RogueCmdBitwiseOr__clone( RogueClassCmdBitwiseOr* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdBitwiseOr__combine_literal_operands( RogueClassCmdBitwiseOr* THIS, RogueClassType* common_type_0 );
RogueClassCmdBitwiseOr* RogueCmdBitwiseOr__init_object( RogueClassCmdBitwiseOr* THIS );
RogueString* RogueCmdBitwiseOr__symbol( RogueClassCmdBitwiseOr* THIS );
RogueString* RogueCmdBitwiseAnd__type_name( RogueClassCmdBitwiseAnd* THIS );
RogueClassCmd* RogueCmdBitwiseAnd__clone( RogueClassCmdBitwiseAnd* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdBitwiseAnd__combine_literal_operands( RogueClassCmdBitwiseAnd* THIS, RogueClassType* common_type_0 );
RogueClassCmdBitwiseAnd* RogueCmdBitwiseAnd__init_object( RogueClassCmdBitwiseAnd* THIS );
RogueString* RogueCmdBitwiseAnd__symbol( RogueClassCmdBitwiseAnd* THIS );
RogueString* RogueCmdBitwiseShiftLeft__type_name( RogueClassCmdBitwiseShiftLeft* THIS );
RogueClassCmd* RogueCmdBitwiseShiftLeft__clone( RogueClassCmdBitwiseShiftLeft* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdBitwiseShiftLeft__combine_literal_operands( RogueClassCmdBitwiseShiftLeft* THIS, RogueClassType* common_type_0 );
RogueClassCmdBitwiseShiftLeft* RogueCmdBitwiseShiftLeft__init_object( RogueClassCmdBitwiseShiftLeft* THIS );
RogueString* RogueCmdBitwiseShiftLeft__cpp_symbol( RogueClassCmdBitwiseShiftLeft* THIS );
RogueString* RogueCmdBitwiseShiftLeft__symbol( RogueClassCmdBitwiseShiftLeft* THIS );
RogueString* RogueCmdBitwiseShiftRight__type_name( RogueClassCmdBitwiseShiftRight* THIS );
RogueClassCmd* RogueCmdBitwiseShiftRight__clone( RogueClassCmdBitwiseShiftRight* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdBitwiseShiftRight__combine_literal_operands( RogueClassCmdBitwiseShiftRight* THIS, RogueClassType* common_type_0 );
void RogueCmdBitwiseShiftRight__write_cpp( RogueClassCmdBitwiseShiftRight* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmdBitwiseShiftRight* RogueCmdBitwiseShiftRight__init_object( RogueClassCmdBitwiseShiftRight* THIS );
RogueString* RogueCmdBitwiseShiftRight__cpp_symbol( RogueClassCmdBitwiseShiftRight* THIS );
RogueString* RogueCmdBitwiseShiftRight__symbol( RogueClassCmdBitwiseShiftRight* THIS );
RogueString* RogueCmdBitwiseShiftRightX__type_name( RogueClassCmdBitwiseShiftRightX* THIS );
RogueClassCmd* RogueCmdBitwiseShiftRightX__clone( RogueClassCmdBitwiseShiftRightX* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdBitwiseShiftRightX__combine_literal_operands( RogueClassCmdBitwiseShiftRightX* THIS, RogueClassType* common_type_0 );
RogueClassCmdBitwiseShiftRightX* RogueCmdBitwiseShiftRightX__init_object( RogueClassCmdBitwiseShiftRightX* THIS );
RogueString* RogueCmdBitwiseShiftRightX__cpp_symbol( RogueClassCmdBitwiseShiftRightX* THIS );
RogueString* RogueCmdBitwiseShiftRightX__symbol( RogueClassCmdBitwiseShiftRightX* THIS );
RogueString* RogueCmdSubtract__type_name( RogueClassCmdSubtract* THIS );
RogueClassCmd* RogueCmdSubtract__clone( RogueClassCmdSubtract* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdSubtract__combine_literal_operands( RogueClassCmdSubtract* THIS, RogueClassType* common_type_0 );
RogueClassCmdSubtract* RogueCmdSubtract__init_object( RogueClassCmdSubtract* THIS );
RogueString* RogueCmdSubtract__fn_name( RogueClassCmdSubtract* THIS );
RogueString* RogueCmdSubtract__symbol( RogueClassCmdSubtract* THIS );
RogueString* RogueCmdMultiply__type_name( RogueClassCmdMultiply* THIS );
RogueClassCmd* RogueCmdMultiply__clone( RogueClassCmdMultiply* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdMultiply__combine_literal_operands( RogueClassCmdMultiply* THIS, RogueClassType* common_type_0 );
RogueClassCmdMultiply* RogueCmdMultiply__init_object( RogueClassCmdMultiply* THIS );
RogueString* RogueCmdMultiply__fn_name( RogueClassCmdMultiply* THIS );
RogueString* RogueCmdMultiply__symbol( RogueClassCmdMultiply* THIS );
RogueString* RogueCmdDivide__type_name( RogueClassCmdDivide* THIS );
RogueClassCmd* RogueCmdDivide__clone( RogueClassCmdDivide* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdDivide__combine_literal_operands( RogueClassCmdDivide* THIS, RogueClassType* common_type_0 );
RogueClassCmdDivide* RogueCmdDivide__init_object( RogueClassCmdDivide* THIS );
RogueString* RogueCmdDivide__fn_name( RogueClassCmdDivide* THIS );
RogueString* RogueCmdDivide__symbol( RogueClassCmdDivide* THIS );
RogueString* RogueCmdMod__type_name( RogueClassCmdMod* THIS );
RogueClassCmd* RogueCmdMod__clone( RogueClassCmdMod* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdMod__combine_literal_operands( RogueClassCmdMod* THIS, RogueClassType* common_type_0 );
void RogueCmdMod__write_cpp( RogueClassCmdMod* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmdMod* RogueCmdMod__init_object( RogueClassCmdMod* THIS );
RogueString* RogueCmdMod__fn_name( RogueClassCmdMod* THIS );
RogueString* RogueCmdMod__symbol( RogueClassCmdMod* THIS );
RogueString* RogueCmdPower__type_name( RogueClassCmdPower* THIS );
RogueClassCmd* RogueCmdPower__clone( RogueClassCmdPower* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdPower__combine_literal_operands( RogueClassCmdPower* THIS, RogueClassType* common_type_0 );
void RogueCmdPower__write_cpp( RogueClassCmdPower* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmdPower* RogueCmdPower__init_object( RogueClassCmdPower* THIS );
RogueString* RogueCmdPower__fn_name( RogueClassCmdPower* THIS );
RogueString* RogueCmdPower__symbol( RogueClassCmdPower* THIS );
RogueString* RogueCmdNegate__type_name( RogueClassCmdNegate* THIS );
RogueClassCmd* RogueCmdNegate__clone( RogueClassCmdNegate* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassType* Rogue_CmdNegate__implicit_type( RogueClassCmdNegate* THIS );
RogueClassCmdNegate* RogueCmdNegate__init_object( RogueClassCmdNegate* THIS );
RogueString* RogueCmdNegate__prefix_symbol( RogueClassCmdNegate* THIS );
RogueClassCmd* RogueCmdNegate__resolve_for_literal_operand( RogueClassCmdNegate* THIS, RogueClassScope* scope_0 );
RogueString* RogueCmdNegate__suffix_symbol( RogueClassCmdNegate* THIS );
RogueString* RogueCmdBitwiseNot__type_name( RogueClassCmdBitwiseNot* THIS );
RogueClassCmd* RogueCmdBitwiseNot__clone( RogueClassCmdBitwiseNot* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassType* Rogue_CmdBitwiseNot__type( RogueClassCmdBitwiseNot* THIS );
RogueClassCmdBitwiseNot* RogueCmdBitwiseNot__init_object( RogueClassCmdBitwiseNot* THIS );
RogueString* RogueCmdBitwiseNot__cpp_prefix_symbol( RogueClassCmdBitwiseNot* THIS );
RogueString* RogueCmdBitwiseNot__prefix_symbol( RogueClassCmdBitwiseNot* THIS );
RogueClassCmd* RogueCmdBitwiseNot__resolve_for_literal_operand( RogueClassCmdBitwiseNot* THIS, RogueClassScope* scope_0 );
RogueString* RogueCmdLogicalize__type_name( RogueClassCmdLogicalize* THIS );
RogueClassCmd* RogueCmdLogicalize__clone( RogueClassCmdLogicalize* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdLogicalize__resolve( RogueClassCmdLogicalize* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdLogicalize__type( RogueClassCmdLogicalize* THIS );
RogueClassCmdLogicalize* RogueCmdLogicalize__init_object( RogueClassCmdLogicalize* THIS );
RogueString* RogueCmdLogicalize__cpp_prefix_symbol( RogueClassCmdLogicalize* THIS );
RogueString* RogueCmdLogicalize__cpp_suffix_symbol( RogueClassCmdLogicalize* THIS );
RogueString* RogueCmdLogicalize__prefix_symbol( RogueClassCmdLogicalize* THIS );
RogueClassCmd* RogueCmdLogicalize__resolve_for_literal_operand( RogueClassCmdLogicalize* THIS, RogueClassScope* scope_0 );
RogueString* RogueCmdLogicalize__suffix_symbol( RogueClassCmdLogicalize* THIS );
RogueString* RogueCmdGetOptionalValue__type_name( RogueClassCmdGetOptionalValue* THIS );
RogueClassCmd* RogueCmdGetOptionalValue__clone( RogueClassCmdGetOptionalValue* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdGetOptionalValue__write_cpp( RogueClassCmdGetOptionalValue* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmdGetOptionalValue* RogueCmdGetOptionalValue__resolve( RogueClassCmdGetOptionalValue* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdGetOptionalValue__type( RogueClassCmdGetOptionalValue* THIS );
RogueClassCmdGetOptionalValue* RogueCmdGetOptionalValue__init_object( RogueClassCmdGetOptionalValue* THIS );
RogueClassCmdGetOptionalValue* RogueCmdGetOptionalValue__init( RogueClassCmdGetOptionalValue* THIS, RogueClassToken* _auto_312_0, RogueClassCmd* _auto_313_1 );
RogueString* RogueCmdElementAccess__type_name( RogueClassCmdElementAccess* THIS );
RogueClassCmd* RogueCmdElementAccess__clone( RogueClassCmdElementAccess* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdElementAccess__resolve( RogueClassCmdElementAccess* THIS, RogueClassScope* scope_0 );
RogueClassCmd* RogueCmdElementAccess__resolve_assignment( RogueClassCmdElementAccess* THIS, RogueClassScope* scope_0, RogueClassCmd* new_value_1 );
RogueClassCmd* RogueCmdElementAccess__resolve_modify( RogueClassCmdElementAccess* THIS, RogueClassScope* scope_0, RogueInteger delta_1 );
RogueClassCmdElementAccess* RogueCmdElementAccess__init_object( RogueClassCmdElementAccess* THIS );
RogueClassCmdElementAccess* RogueCmdElementAccess__init( RogueClassCmdElementAccess* THIS, RogueClassToken* _auto_314_0, RogueClassCmd* _auto_315_1, RogueClassCmd* _auto_316_2 );
RogueString* RogueCmdConvertToType__type_name( RogueClassCmdConvertToType* THIS );
RogueClassCmd* RogueCmdConvertToType__clone( RogueClassCmdConvertToType* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdConvertToType__resolve( RogueClassCmdConvertToType* THIS, RogueClassScope* scope_0 );
RogueClassCmdConvertToType* RogueCmdConvertToType__init_object( RogueClassCmdConvertToType* THIS );
RogueString* RogueCmdCreateCallback__type_name( RogueClassCmdCreateCallback* THIS );
RogueClassCmdCreateCallback* RogueCmdCreateCallback__clone( RogueClassCmdCreateCallback* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdCreateCallback__resolve( RogueClassCmdCreateCallback* THIS, RogueClassScope* scope_0 );
RogueClassCmdCreateCallback* RogueCmdCreateCallback__init_object( RogueClassCmdCreateCallback* THIS );
RogueClassCmdCreateCallback* RogueCmdCreateCallback__init( RogueClassCmdCreateCallback* THIS, RogueClassToken* _auto_317_0, RogueClassCmd* _auto_318_1, RogueString* _auto_319_2, RogueString* _auto_320_3, RogueClassType* _auto_321_4 );
RogueString* RogueCmdAs__type_name( RogueClassCmdAs* THIS );
RogueClassCmd* RogueCmdAs__clone( RogueClassCmdAs* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdAs__write_cpp( RogueClassCmdAs* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdAs__resolve( RogueClassCmdAs* THIS, RogueClassScope* scope_0 );
RogueClassCmdAs* RogueCmdAs__init_object( RogueClassCmdAs* THIS );
RogueString* RogueCmdDefaultValue__type_name( RogueClassCmdDefaultValue* THIS );
RogueClassCmd* RogueCmdDefaultValue__clone( RogueClassCmdDefaultValue* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdDefaultValue__resolve( RogueClassCmdDefaultValue* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdDefaultValue__type( RogueClassCmdDefaultValue* THIS );
RogueClassCmdDefaultValue* RogueCmdDefaultValue__init_object( RogueClassCmdDefaultValue* THIS );
RogueClassCmdDefaultValue* RogueCmdDefaultValue__init( RogueClassCmdDefaultValue* THIS, RogueClassToken* _auto_322_0, RogueClassType* _auto_323_1 );
RogueString* RogueCmdFormattedString__type_name( RogueClassCmdFormattedString* THIS );
RogueClassCmd* RogueCmdFormattedString__clone( RogueClassCmdFormattedString* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassType* Rogue_CmdFormattedString__implicit_type( RogueClassCmdFormattedString* THIS );
RogueClassCmd* RogueCmdFormattedString__resolve( RogueClassCmdFormattedString* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdFormattedString__type( RogueClassCmdFormattedString* THIS );
RogueClassCmdFormattedString* RogueCmdFormattedString__init_object( RogueClassCmdFormattedString* THIS );
RogueClassCmdFormattedString* RogueCmdFormattedString__init( RogueClassCmdFormattedString* THIS, RogueClassToken* _auto_325_0, RogueString* _auto_326_1, RogueClassCmdArgs* _auto_327_2 );
RogueString* RogueCmdLiteralString__type_name( RogueClassCmdLiteralString* THIS );
RogueClassCmd* RogueCmdLiteralString__clone( RogueClassCmdLiteralString* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdLiteralString__write_cpp( RogueClassCmdLiteralString* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdLiteralString__resolve( RogueClassCmdLiteralString* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdLiteralString__type( RogueClassCmdLiteralString* THIS );
RogueClassCmdLiteralString* RogueCmdLiteralString__init_object( RogueClassCmdLiteralString* THIS );
RogueClassCmdLiteralString* RogueCmdLiteralString__init( RogueClassCmdLiteralString* THIS, RogueClassToken* _auto_328_0, RogueString* _auto_329_1, RogueInteger _auto_330_2 );
RogueString* RogueCmdLiteralNull__type_name( RogueClassCmdLiteralNull* THIS );
RogueClassCmd* RogueCmdLiteralNull__cast_to( RogueClassCmdLiteralNull* THIS, RogueClassType* target_type_0 );
RogueClassCmd* RogueCmdLiteralNull__clone( RogueClassCmdLiteralNull* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdLiteralNull__write_cpp( RogueClassCmdLiteralNull* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmdLiteralNull* RogueCmdLiteralNull__resolve( RogueClassCmdLiteralNull* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdLiteralNull__type( RogueClassCmdLiteralNull* THIS );
RogueClassCmdLiteralNull* RogueCmdLiteralNull__init_object( RogueClassCmdLiteralNull* THIS );
RogueClassCmdLiteralNull* RogueCmdLiteralNull__init( RogueClassCmdLiteralNull* THIS, RogueClassToken* _auto_331_0 );
RogueString* RogueCmdLiteralReal__type_name( RogueClassCmdLiteralReal* THIS );
RogueClassCmd* RogueCmdLiteralReal__clone( RogueClassCmdLiteralReal* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdLiteralReal__write_cpp( RogueClassCmdLiteralReal* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdLiteralReal__resolve( RogueClassCmdLiteralReal* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdLiteralReal__type( RogueClassCmdLiteralReal* THIS );
RogueClassCmdLiteralReal* RogueCmdLiteralReal__init_object( RogueClassCmdLiteralReal* THIS );
RogueClassCmdLiteralReal* RogueCmdLiteralReal__init( RogueClassCmdLiteralReal* THIS, RogueClassToken* _auto_332_0, RogueReal _auto_333_1 );
RogueString* RogueCmdLiteralCharacter__type_name( RogueClassCmdLiteralCharacter* THIS );
RogueClassCmd* RogueCmdLiteralCharacter__clone( RogueClassCmdLiteralCharacter* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdLiteralCharacter__write_cpp( RogueClassCmdLiteralCharacter* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdLiteralCharacter__resolve( RogueClassCmdLiteralCharacter* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdLiteralCharacter__type( RogueClassCmdLiteralCharacter* THIS );
RogueClassCmdLiteralCharacter* RogueCmdLiteralCharacter__init_object( RogueClassCmdLiteralCharacter* THIS );
RogueClassCmdLiteralCharacter* RogueCmdLiteralCharacter__init( RogueClassCmdLiteralCharacter* THIS, RogueClassToken* _auto_334_0, RogueCharacter _auto_335_1 );
RogueString* RogueCmdLiteralThis__type_name( RogueClassCmdLiteralThis* THIS );
RogueClassCmd* RogueCmdLiteralThis__clone( RogueClassCmdLiteralThis* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdLiteralThis__require_type_context( RogueClassCmdLiteralThis* THIS );
RogueClassCmd* RogueCmdLiteralThis__resolve( RogueClassCmdLiteralThis* THIS, RogueClassScope* scope_0 );
RogueClassCmdLiteralThis* RogueCmdLiteralThis__init_object( RogueClassCmdLiteralThis* THIS );
RogueString* RogueCmdThisContext__type_name( RogueClassCmdThisContext* THIS );
RogueClassCmd* RogueCmdThisContext__clone( RogueClassCmdThisContext* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassType* Rogue_CmdThisContext__implicit_type( RogueClassCmdThisContext* THIS );
void RogueCmdThisContext__write_cpp( RogueClassCmdThisContext* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
void RogueCmdThisContext__require_type_context( RogueClassCmdThisContext* THIS );
RogueClassCmd* RogueCmdThisContext__resolve( RogueClassCmdThisContext* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdThisContext__type( RogueClassCmdThisContext* THIS );
RogueClassCmdThisContext* RogueCmdThisContext__init_object( RogueClassCmdThisContext* THIS );
RogueClassCmdThisContext* RogueCmdThisContext__init( RogueClassCmdThisContext* THIS, RogueClassToken* _auto_336_0, RogueClassType* _auto_337_1 );
RogueString* RogueCmdLiteralLogical__type_name( RogueClassCmdLiteralLogical* THIS );
RogueClassCmd* RogueCmdLiteralLogical__clone( RogueClassCmdLiteralLogical* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdLiteralLogical__write_cpp( RogueClassCmdLiteralLogical* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdLiteralLogical__resolve( RogueClassCmdLiteralLogical* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdLiteralLogical__type( RogueClassCmdLiteralLogical* THIS );
RogueClassCmdLiteralLogical* RogueCmdLiteralLogical__init_object( RogueClassCmdLiteralLogical* THIS );
RogueClassCmdLiteralLogical* RogueCmdLiteralLogical__init( RogueClassCmdLiteralLogical* THIS, RogueClassToken* _auto_338_0, RogueLogical _auto_339_1 );
RogueString* RogueCmdCreateList__type_name( RogueClassCmdCreateList* THIS );
RogueClassCmd* RogueCmdCreateList__clone( RogueClassCmdCreateList* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdCreateList__resolve( RogueClassCmdCreateList* THIS, RogueClassScope* scope_0 );
RogueClassCmdCreateList* RogueCmdCreateList__init_object( RogueClassCmdCreateList* THIS );
RogueClassCmdCreateList* RogueCmdCreateList__init( RogueClassCmdCreateList* THIS, RogueClassToken* _auto_340_0, RogueClassCmdArgs* _auto_341_1, RogueClassType* _auto_342_2 );
RogueString* RogueCmdCallPriorMethod__type_name( RogueClassCmdCallPriorMethod* THIS );
RogueClassCmd* RogueCmdCallPriorMethod__clone( RogueClassCmdCallPriorMethod* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdCallPriorMethod__resolve( RogueClassCmdCallPriorMethod* THIS, RogueClassScope* scope_0 );
RogueClassCmdCallPriorMethod* RogueCmdCallPriorMethod__init_object( RogueClassCmdCallPriorMethod* THIS );
RogueClassCmdCallPriorMethod* RogueCmdCallPriorMethod__init( RogueClassCmdCallPriorMethod* THIS, RogueClassToken* _auto_343_0, RogueString* _auto_344_1, RogueClassCmdArgs* _auto_345_2 );
RogueString* RogueFnParamList__to_String( RogueFnParamList* THIS );
RogueString* RogueFnParamList__type_name( RogueFnParamList* THIS );
RogueFnParamList* RogueFnParamList__init_object( RogueFnParamList* THIS );
RogueFnParamList* RogueFnParamList__init( RogueFnParamList* THIS );
RogueFnParamList* RogueFnParamList__init( RogueFnParamList* THIS, RogueInteger initial_capacity_0 );
RogueFnParamList* RogueFnParamList__init( RogueFnParamList* THIS, RogueInteger initial_capacity_0, RogueClassFnParam* initial_value_1 );
RogueFnParamList* RogueFnParamList__clone( RogueFnParamList* THIS );
RogueFnParamList* RogueFnParamList__add( RogueFnParamList* THIS, RogueClassFnParam* value_0 );
RogueFnParamList* RogueFnParamList__add( RogueFnParamList* THIS, RogueFnParamList* other_0 );
RogueInteger RogueFnParamList__capacity( RogueFnParamList* THIS );
RogueFnParamList* RogueFnParamList__clear( RogueFnParamList* THIS );
RogueFnParamList* RogueFnParamList__insert( RogueFnParamList* THIS, RogueClassFnParam* value_0, RogueInteger before_index_1 );
RogueClassFnParam* RogueFnParamList__last( RogueFnParamList* THIS );
RogueClassFnParam* RogueFnParamList__random( RogueFnParamList* THIS );
RogueFnParamList* RogueFnParamList__reserve( RogueFnParamList* THIS, RogueInteger additional_count_0 );
RogueClassFnParam* RogueFnParamList__remove( RogueFnParamList* THIS, RogueClassFnParam* value_0 );
RogueClassFnParam* RogueFnParamList__remove_at( RogueFnParamList* THIS, RogueInteger index_0 );
RogueClassFnParam* RogueFnParamList__remove_last( RogueFnParamList* THIS );
RogueString* RogueFnParam__type_name( RogueClassFnParam* THIS );
RogueClassFnParam* RogueFnParam__init( RogueClassFnParam* THIS, RogueString* _auto_346_0 );
RogueClassFnParam* RogueFnParam__init_object( RogueClassFnParam* THIS );
RogueString* RogueFnArgList__to_String( RogueFnArgList* THIS );
RogueString* RogueFnArgList__type_name( RogueFnArgList* THIS );
RogueFnArgList* RogueFnArgList__init_object( RogueFnArgList* THIS );
RogueFnArgList* RogueFnArgList__init( RogueFnArgList* THIS );
RogueFnArgList* RogueFnArgList__init( RogueFnArgList* THIS, RogueInteger initial_capacity_0 );
RogueFnArgList* RogueFnArgList__init( RogueFnArgList* THIS, RogueInteger initial_capacity_0, RogueClassFnArg* initial_value_1 );
RogueFnArgList* RogueFnArgList__clone( RogueFnArgList* THIS );
RogueFnArgList* RogueFnArgList__add( RogueFnArgList* THIS, RogueClassFnArg* value_0 );
RogueFnArgList* RogueFnArgList__add( RogueFnArgList* THIS, RogueFnArgList* other_0 );
RogueInteger RogueFnArgList__capacity( RogueFnArgList* THIS );
RogueFnArgList* RogueFnArgList__clear( RogueFnArgList* THIS );
RogueFnArgList* RogueFnArgList__insert( RogueFnArgList* THIS, RogueClassFnArg* value_0, RogueInteger before_index_1 );
RogueClassFnArg* RogueFnArgList__last( RogueFnArgList* THIS );
RogueClassFnArg* RogueFnArgList__random( RogueFnArgList* THIS );
RogueFnArgList* RogueFnArgList__reserve( RogueFnArgList* THIS, RogueInteger additional_count_0 );
RogueClassFnArg* RogueFnArgList__remove( RogueFnArgList* THIS, RogueClassFnArg* value_0 );
RogueClassFnArg* RogueFnArgList__remove_at( RogueFnArgList* THIS, RogueInteger index_0 );
RogueClassFnArg* RogueFnArgList__remove_last( RogueFnArgList* THIS );
RogueString* RogueFnArg__type_name( RogueClassFnArg* THIS );
RogueClassFnArg* RogueFnArg__init( RogueClassFnArg* THIS, RogueString* _auto_348_0, RogueClassCmd* _auto_349_1 );
RogueClassFnArg* Rogue_FnArg__set_type( RogueClassFnArg* THIS, RogueClassType* _auto_350_0 );
RogueClassFnArg* RogueFnArg__init_object( RogueClassFnArg* THIS );
RogueString* RogueCmdCreateFunction__type_name( RogueClassCmdCreateFunction* THIS );
RogueClassCmdCreateFunction* RogueCmdCreateFunction__clone( RogueClassCmdCreateFunction* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassCmd* RogueCmdCreateFunction__resolve( RogueClassCmdCreateFunction* THIS, RogueClassScope* scope_0 );
RogueClassCmdCreateFunction* RogueCmdCreateFunction__init_object( RogueClassCmdCreateFunction* THIS );
RogueClassCmdCreateFunction* RogueCmdCreateFunction__init( RogueClassCmdCreateFunction* THIS, RogueClassToken* _auto_352_0, RogueFnParamList* _auto_353_1, RogueClassType* _auto_354_2, RogueFnArgList* _auto_355_3, RogueClassCmdStatementList* _auto_356_4 );
RogueString* RogueTokenArray__type_name( RogueArray* THIS );
RogueString* RogueTokenListOps__type_name( RogueClassTokenListOps* THIS );
RogueClassTokenListOps* RogueTokenListOps__init_object( RogueClassTokenListOps* THIS );
RogueString* RogueTemplateListOps__type_name( RogueClassTemplateListOps* THIS );
RogueClassTemplateListOps* RogueTemplateListOps__init_object( RogueClassTemplateListOps* THIS );
RogueString* RogueTypeParameterList__to_String( RogueTypeParameterList* THIS );
RogueString* RogueTypeParameterList__type_name( RogueTypeParameterList* THIS );
RogueTypeParameterList* RogueTypeParameterList__init_object( RogueTypeParameterList* THIS );
RogueTypeParameterList* RogueTypeParameterList__init( RogueTypeParameterList* THIS );
RogueTypeParameterList* RogueTypeParameterList__init( RogueTypeParameterList* THIS, RogueInteger initial_capacity_0 );
RogueTypeParameterList* RogueTypeParameterList__init( RogueTypeParameterList* THIS, RogueInteger initial_capacity_0, RogueClassTypeParameter* initial_value_1 );
RogueTypeParameterList* RogueTypeParameterList__clone( RogueTypeParameterList* THIS );
RogueTypeParameterList* RogueTypeParameterList__add( RogueTypeParameterList* THIS, RogueClassTypeParameter* value_0 );
RogueTypeParameterList* RogueTypeParameterList__add( RogueTypeParameterList* THIS, RogueTypeParameterList* other_0 );
RogueInteger RogueTypeParameterList__capacity( RogueTypeParameterList* THIS );
RogueTypeParameterList* RogueTypeParameterList__clear( RogueTypeParameterList* THIS );
RogueTypeParameterList* RogueTypeParameterList__insert( RogueTypeParameterList* THIS, RogueClassTypeParameter* value_0, RogueInteger before_index_1 );
RogueClassTypeParameter* RogueTypeParameterList__last( RogueTypeParameterList* THIS );
RogueClassTypeParameter* RogueTypeParameterList__random( RogueTypeParameterList* THIS );
RogueTypeParameterList* RogueTypeParameterList__reserve( RogueTypeParameterList* THIS, RogueInteger additional_count_0 );
RogueClassTypeParameter* RogueTypeParameterList__remove( RogueTypeParameterList* THIS, RogueClassTypeParameter* value_0 );
RogueClassTypeParameter* RogueTypeParameterList__remove_at( RogueTypeParameterList* THIS, RogueInteger index_0 );
RogueClassTypeParameter* RogueTypeParameterList__remove_last( RogueTypeParameterList* THIS );
RogueString* RogueString_TypeSpecializerTable__to_String( RogueClassString_TypeSpecializerTable* THIS );
RogueString* RogueString_TypeSpecializerTable__type_name( RogueClassString_TypeSpecializerTable* THIS );
RogueClassString_TypeSpecializerTable* RogueString_TypeSpecializerTable__init( RogueClassString_TypeSpecializerTable* THIS );
RogueClassString_TypeSpecializerTable* RogueString_TypeSpecializerTable__init( RogueClassString_TypeSpecializerTable* THIS, RogueInteger bin_count_0 );
RogueClassString_TypeSpecializerTable* RogueString_TypeSpecializerTable__init( RogueClassString_TypeSpecializerTable* THIS, RogueClassString_TypeSpecializerTable* other_0 );
RogueClassString_TypeSpecializerTable* RogueString_TypeSpecializerTable__add( RogueClassString_TypeSpecializerTable* THIS, RogueClassString_TypeSpecializerTable* other_0 );
RogueClassTypeSpecializer* RogueString_TypeSpecializerTable__at( RogueClassString_TypeSpecializerTable* THIS, RogueInteger index_0 );
void RogueString_TypeSpecializerTable__clear( RogueClassString_TypeSpecializerTable* THIS );
RogueClassString_TypeSpecializerTable* RogueString_TypeSpecializerTable__clone( RogueClassString_TypeSpecializerTable* THIS );
RogueLogical RogueString_TypeSpecializerTable__contains( RogueClassString_TypeSpecializerTable* THIS, RogueString* key_0 );
RogueInteger RogueString_TypeSpecializerTable__count( RogueClassString_TypeSpecializerTable* THIS );
RogueClassString_TypeSpecializerTableEntry* RogueString_TypeSpecializerTable__find( RogueClassString_TypeSpecializerTable* THIS, RogueString* key_0 );
RogueClassTypeSpecializer* RogueString_TypeSpecializerTable__get( RogueClassString_TypeSpecializerTable* THIS, RogueString* key_0 );
RogueClassTypeSpecializer* RogueString_TypeSpecializerTable__remove( RogueClassString_TypeSpecializerTable* THIS, RogueString* key_0 );
void RogueString_TypeSpecializerTable__set( RogueClassString_TypeSpecializerTable* THIS, RogueString* key_0, RogueClassTypeSpecializer* value_1 );
RogueStringBuilder* RogueString_TypeSpecializerTable__print_to( RogueClassString_TypeSpecializerTable* THIS, RogueStringBuilder* buffer_0 );
RogueClassString_TypeSpecializerTable* RogueString_TypeSpecializerTable__init_object( RogueClassString_TypeSpecializerTable* THIS );
RogueString* RogueTypeParameterArray__type_name( RogueArray* THIS );
RogueString* RogueTypeSpecializer__type_name( RogueClassTypeSpecializer* THIS );
RogueClassTypeSpecializer* RogueTypeSpecializer__init( RogueClassTypeSpecializer* THIS, RogueString* _auto_374_0, RogueInteger _auto_375_1 );
RogueClassTypeSpecializer* RogueTypeSpecializer__init_object( RogueClassTypeSpecializer* THIS );
RogueString* RogueString_TemplateTableEntryList__to_String( RogueString_TemplateTableEntryList* THIS );
RogueString* RogueString_TemplateTableEntryList__type_name( RogueString_TemplateTableEntryList* THIS );
RogueString_TemplateTableEntryList* RogueString_TemplateTableEntryList__init_object( RogueString_TemplateTableEntryList* THIS );
RogueString_TemplateTableEntryList* RogueString_TemplateTableEntryList__init( RogueString_TemplateTableEntryList* THIS );
RogueString_TemplateTableEntryList* RogueString_TemplateTableEntryList__init( RogueString_TemplateTableEntryList* THIS, RogueInteger initial_capacity_0 );
RogueString_TemplateTableEntryList* RogueString_TemplateTableEntryList__init( RogueString_TemplateTableEntryList* THIS, RogueInteger initial_capacity_0, RogueClassString_TemplateTableEntry* initial_value_1 );
RogueString_TemplateTableEntryList* RogueString_TemplateTableEntryList__clone( RogueString_TemplateTableEntryList* THIS );
RogueString_TemplateTableEntryList* RogueString_TemplateTableEntryList__add( RogueString_TemplateTableEntryList* THIS, RogueClassString_TemplateTableEntry* value_0 );
RogueString_TemplateTableEntryList* RogueString_TemplateTableEntryList__add( RogueString_TemplateTableEntryList* THIS, RogueString_TemplateTableEntryList* other_0 );
RogueInteger RogueString_TemplateTableEntryList__capacity( RogueString_TemplateTableEntryList* THIS );
RogueString_TemplateTableEntryList* RogueString_TemplateTableEntryList__clear( RogueString_TemplateTableEntryList* THIS );
RogueString_TemplateTableEntryList* RogueString_TemplateTableEntryList__insert( RogueString_TemplateTableEntryList* THIS, RogueClassString_TemplateTableEntry* value_0, RogueInteger before_index_1 );
RogueClassString_TemplateTableEntry* RogueString_TemplateTableEntryList__last( RogueString_TemplateTableEntryList* THIS );
RogueClassString_TemplateTableEntry* RogueString_TemplateTableEntryList__random( RogueString_TemplateTableEntryList* THIS );
RogueString_TemplateTableEntryList* RogueString_TemplateTableEntryList__reserve( RogueString_TemplateTableEntryList* THIS, RogueInteger additional_count_0 );
RogueClassString_TemplateTableEntry* RogueString_TemplateTableEntryList__remove( RogueString_TemplateTableEntryList* THIS, RogueClassString_TemplateTableEntry* value_0 );
RogueClassString_TemplateTableEntry* RogueString_TemplateTableEntryList__remove_at( RogueString_TemplateTableEntryList* THIS, RogueInteger index_0 );
RogueClassString_TemplateTableEntry* RogueString_TemplateTableEntryList__remove_last( RogueString_TemplateTableEntryList* THIS );
RogueString* RogueString_TemplateTableEntry__type_name( RogueClassString_TemplateTableEntry* THIS );
RogueClassString_TemplateTableEntry* RogueString_TemplateTableEntry__init( RogueClassString_TemplateTableEntry* THIS, RogueString* _key_0, RogueClassTemplate* _value_1, RogueInteger _hash_2 );
RogueClassString_TemplateTableEntry* RogueString_TemplateTableEntry__init_object( RogueClassString_TemplateTableEntry* THIS );
RogueString* RogueString_TemplateTableEntryArray__type_name( RogueArray* THIS );
RogueString* RogueString_PropertyTable__to_String( RogueClassString_PropertyTable* THIS );
RogueString* RogueString_PropertyTable__type_name( RogueClassString_PropertyTable* THIS );
RogueClassString_PropertyTable* RogueString_PropertyTable__init( RogueClassString_PropertyTable* THIS );
RogueClassString_PropertyTable* RogueString_PropertyTable__init( RogueClassString_PropertyTable* THIS, RogueInteger bin_count_0 );
RogueClassString_PropertyTable* RogueString_PropertyTable__init( RogueClassString_PropertyTable* THIS, RogueClassString_PropertyTable* other_0 );
RogueClassString_PropertyTable* RogueString_PropertyTable__add( RogueClassString_PropertyTable* THIS, RogueClassString_PropertyTable* other_0 );
RogueClassProperty* RogueString_PropertyTable__at( RogueClassString_PropertyTable* THIS, RogueInteger index_0 );
void RogueString_PropertyTable__clear( RogueClassString_PropertyTable* THIS );
RogueClassString_PropertyTable* RogueString_PropertyTable__clone( RogueClassString_PropertyTable* THIS );
RogueLogical RogueString_PropertyTable__contains( RogueClassString_PropertyTable* THIS, RogueString* key_0 );
RogueInteger RogueString_PropertyTable__count( RogueClassString_PropertyTable* THIS );
RogueClassString_PropertyTableEntry* RogueString_PropertyTable__find( RogueClassString_PropertyTable* THIS, RogueString* key_0 );
RogueClassProperty* RogueString_PropertyTable__get( RogueClassString_PropertyTable* THIS, RogueString* key_0 );
RogueClassProperty* RogueString_PropertyTable__remove( RogueClassString_PropertyTable* THIS, RogueString* key_0 );
void RogueString_PropertyTable__set( RogueClassString_PropertyTable* THIS, RogueString* key_0, RogueClassProperty* value_1 );
RogueStringBuilder* RogueString_PropertyTable__print_to( RogueClassString_PropertyTable* THIS, RogueStringBuilder* buffer_0 );
RogueClassString_PropertyTable* RogueString_PropertyTable__init_object( RogueClassString_PropertyTable* THIS );
RogueString* RogueString_MethodListTable__to_String( RogueClassString_MethodListTable* THIS );
RogueString* RogueString_MethodListTable__type_name( RogueClassString_MethodListTable* THIS );
RogueClassString_MethodListTable* RogueString_MethodListTable__init( RogueClassString_MethodListTable* THIS );
RogueClassString_MethodListTable* RogueString_MethodListTable__init( RogueClassString_MethodListTable* THIS, RogueInteger bin_count_0 );
RogueClassString_MethodListTable* RogueString_MethodListTable__init( RogueClassString_MethodListTable* THIS, RogueClassString_MethodListTable* other_0 );
RogueClassString_MethodListTable* RogueString_MethodListTable__add( RogueClassString_MethodListTable* THIS, RogueClassString_MethodListTable* other_0 );
RogueMethodList* RogueString_MethodListTable__at( RogueClassString_MethodListTable* THIS, RogueInteger index_0 );
void RogueString_MethodListTable__clear( RogueClassString_MethodListTable* THIS );
RogueClassString_MethodListTable* RogueString_MethodListTable__clone( RogueClassString_MethodListTable* THIS );
RogueLogical RogueString_MethodListTable__contains( RogueClassString_MethodListTable* THIS, RogueString* key_0 );
RogueInteger RogueString_MethodListTable__count( RogueClassString_MethodListTable* THIS );
RogueClassString_MethodListTableEntry* RogueString_MethodListTable__find( RogueClassString_MethodListTable* THIS, RogueString* key_0 );
RogueMethodList* RogueString_MethodListTable__get( RogueClassString_MethodListTable* THIS, RogueString* key_0 );
RogueMethodList* RogueString_MethodListTable__remove( RogueClassString_MethodListTable* THIS, RogueString* key_0 );
void RogueString_MethodListTable__set( RogueClassString_MethodListTable* THIS, RogueString* key_0, RogueMethodList* value_1 );
RogueStringBuilder* RogueString_MethodListTable__print_to( RogueClassString_MethodListTable* THIS, RogueStringBuilder* buffer_0 );
RogueClassString_MethodListTable* RogueString_MethodListTable__init_object( RogueClassString_MethodListTable* THIS );
RogueString* RogueCmdCreateCompound__type_name( RogueClassCmdCreateCompound* THIS );
RogueClassCmd* RogueCmdCreateCompound__clone( RogueClassCmdCreateCompound* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdCreateCompound__write_cpp( RogueClassCmdCreateCompound* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdCreateCompound__resolve( RogueClassCmdCreateCompound* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdCreateCompound__type( RogueClassCmdCreateCompound* THIS );
RogueClassCmdCreateCompound* RogueCmdCreateCompound__init_object( RogueClassCmdCreateCompound* THIS );
RogueClassCmdCreateCompound* RogueCmdCreateCompound__init( RogueClassCmdCreateCompound* THIS, RogueClassToken* _auto_388_0, RogueClassType* _auto_389_1, RogueClassCmdArgs* _auto_390_2 );
RogueString* RogueCmdWriteSetting__type_name( RogueClassCmdWriteSetting* THIS );
RogueClassCmd* RogueCmdWriteSetting__clone( RogueClassCmdWriteSetting* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdWriteSetting__write_cpp( RogueClassCmdWriteSetting* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdWriteSetting__resolve( RogueClassCmdWriteSetting* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdWriteSetting__type( RogueClassCmdWriteSetting* THIS );
RogueClassCmdWriteSetting* RogueCmdWriteSetting__init_object( RogueClassCmdWriteSetting* THIS );
RogueClassCmdWriteSetting* RogueCmdWriteSetting__init( RogueClassCmdWriteSetting* THIS, RogueClassToken* _auto_412_0, RogueClassProperty* _auto_413_1, RogueClassCmd* _auto_414_2 );
RogueString* RogueMethodListOps__type_name( RogueClassMethodListOps* THIS );
RogueClassMethodListOps* RogueMethodListOps__init_object( RogueClassMethodListOps* THIS );
RogueString* RogueCmdWriteProperty__type_name( RogueClassCmdWriteProperty* THIS );
RogueClassCmd* RogueCmdWriteProperty__clone( RogueClassCmdWriteProperty* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdWriteProperty__write_cpp( RogueClassCmdWriteProperty* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdWriteProperty__resolve( RogueClassCmdWriteProperty* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdWriteProperty__type( RogueClassCmdWriteProperty* THIS );
RogueClassCmdWriteProperty* RogueCmdWriteProperty__init_object( RogueClassCmdWriteProperty* THIS );
RogueClassCmdWriteProperty* RogueCmdWriteProperty__init( RogueClassCmdWriteProperty* THIS, RogueClassToken* _auto_415_0, RogueClassCmd* _auto_416_1, RogueClassProperty* _auto_417_2, RogueClassCmd* _auto_418_3 );
RogueString* RogueTypeListOps__type_name( RogueClassTypeListOps* THIS );
RogueClassTypeListOps* RogueTypeListOps__init_object( RogueClassTypeListOps* THIS );
RogueString* RoguePropertyListOps__type_name( RogueClassPropertyListOps* THIS );
RogueClassPropertyListOps* RoguePropertyListOps__init_object( RogueClassPropertyListOps* THIS );
RogueString* RogueString_TypeTableEntryList__to_String( RogueString_TypeTableEntryList* THIS );
RogueString* RogueString_TypeTableEntryList__type_name( RogueString_TypeTableEntryList* THIS );
RogueString_TypeTableEntryList* RogueString_TypeTableEntryList__init_object( RogueString_TypeTableEntryList* THIS );
RogueString_TypeTableEntryList* RogueString_TypeTableEntryList__init( RogueString_TypeTableEntryList* THIS );
RogueString_TypeTableEntryList* RogueString_TypeTableEntryList__init( RogueString_TypeTableEntryList* THIS, RogueInteger initial_capacity_0 );
RogueString_TypeTableEntryList* RogueString_TypeTableEntryList__init( RogueString_TypeTableEntryList* THIS, RogueInteger initial_capacity_0, RogueClassString_TypeTableEntry* initial_value_1 );
RogueString_TypeTableEntryList* RogueString_TypeTableEntryList__clone( RogueString_TypeTableEntryList* THIS );
RogueString_TypeTableEntryList* RogueString_TypeTableEntryList__add( RogueString_TypeTableEntryList* THIS, RogueClassString_TypeTableEntry* value_0 );
RogueString_TypeTableEntryList* RogueString_TypeTableEntryList__add( RogueString_TypeTableEntryList* THIS, RogueString_TypeTableEntryList* other_0 );
RogueInteger RogueString_TypeTableEntryList__capacity( RogueString_TypeTableEntryList* THIS );
RogueString_TypeTableEntryList* RogueString_TypeTableEntryList__clear( RogueString_TypeTableEntryList* THIS );
RogueString_TypeTableEntryList* RogueString_TypeTableEntryList__insert( RogueString_TypeTableEntryList* THIS, RogueClassString_TypeTableEntry* value_0, RogueInteger before_index_1 );
RogueClassString_TypeTableEntry* RogueString_TypeTableEntryList__last( RogueString_TypeTableEntryList* THIS );
RogueClassString_TypeTableEntry* RogueString_TypeTableEntryList__random( RogueString_TypeTableEntryList* THIS );
RogueString_TypeTableEntryList* RogueString_TypeTableEntryList__reserve( RogueString_TypeTableEntryList* THIS, RogueInteger additional_count_0 );
RogueClassString_TypeTableEntry* RogueString_TypeTableEntryList__remove( RogueString_TypeTableEntryList* THIS, RogueClassString_TypeTableEntry* value_0 );
RogueClassString_TypeTableEntry* RogueString_TypeTableEntryList__remove_at( RogueString_TypeTableEntryList* THIS, RogueInteger index_0 );
RogueClassString_TypeTableEntry* RogueString_TypeTableEntryList__remove_last( RogueString_TypeTableEntryList* THIS );
RogueString* RogueString_TypeTableEntry__type_name( RogueClassString_TypeTableEntry* THIS );
RogueClassString_TypeTableEntry* RogueString_TypeTableEntry__init( RogueClassString_TypeTableEntry* THIS, RogueString* _key_0, RogueClassType* _value_1, RogueInteger _hash_2 );
RogueClassString_TypeTableEntry* RogueString_TypeTableEntry__init_object( RogueClassString_TypeTableEntry* THIS );
RogueString* RogueString_TypeTableEntryArray__type_name( RogueArray* THIS );
RogueString* RogueString_IntegerTableEntryList__to_String( RogueString_IntegerTableEntryList* THIS );
RogueString* RogueString_IntegerTableEntryList__type_name( RogueString_IntegerTableEntryList* THIS );
RogueString_IntegerTableEntryList* RogueString_IntegerTableEntryList__init_object( RogueString_IntegerTableEntryList* THIS );
RogueString_IntegerTableEntryList* RogueString_IntegerTableEntryList__init( RogueString_IntegerTableEntryList* THIS );
RogueString_IntegerTableEntryList* RogueString_IntegerTableEntryList__init( RogueString_IntegerTableEntryList* THIS, RogueInteger initial_capacity_0 );
RogueString_IntegerTableEntryList* RogueString_IntegerTableEntryList__init( RogueString_IntegerTableEntryList* THIS, RogueInteger initial_capacity_0, RogueClassString_IntegerTableEntry* initial_value_1 );
RogueString_IntegerTableEntryList* RogueString_IntegerTableEntryList__clone( RogueString_IntegerTableEntryList* THIS );
RogueString_IntegerTableEntryList* RogueString_IntegerTableEntryList__add( RogueString_IntegerTableEntryList* THIS, RogueClassString_IntegerTableEntry* value_0 );
RogueString_IntegerTableEntryList* RogueString_IntegerTableEntryList__add( RogueString_IntegerTableEntryList* THIS, RogueString_IntegerTableEntryList* other_0 );
RogueInteger RogueString_IntegerTableEntryList__capacity( RogueString_IntegerTableEntryList* THIS );
RogueString_IntegerTableEntryList* RogueString_IntegerTableEntryList__clear( RogueString_IntegerTableEntryList* THIS );
RogueString_IntegerTableEntryList* RogueString_IntegerTableEntryList__insert( RogueString_IntegerTableEntryList* THIS, RogueClassString_IntegerTableEntry* value_0, RogueInteger before_index_1 );
RogueClassString_IntegerTableEntry* RogueString_IntegerTableEntryList__last( RogueString_IntegerTableEntryList* THIS );
RogueClassString_IntegerTableEntry* RogueString_IntegerTableEntryList__random( RogueString_IntegerTableEntryList* THIS );
RogueString_IntegerTableEntryList* RogueString_IntegerTableEntryList__reserve( RogueString_IntegerTableEntryList* THIS, RogueInteger additional_count_0 );
RogueClassString_IntegerTableEntry* RogueString_IntegerTableEntryList__remove( RogueString_IntegerTableEntryList* THIS, RogueClassString_IntegerTableEntry* value_0 );
RogueClassString_IntegerTableEntry* RogueString_IntegerTableEntryList__remove_at( RogueString_IntegerTableEntryList* THIS, RogueInteger index_0 );
RogueClassString_IntegerTableEntry* RogueString_IntegerTableEntryList__remove_last( RogueString_IntegerTableEntryList* THIS );
RogueString* RogueString_IntegerTableEntry__type_name( RogueClassString_IntegerTableEntry* THIS );
RogueClassString_IntegerTableEntry* RogueString_IntegerTableEntry__init( RogueClassString_IntegerTableEntry* THIS, RogueString* _key_0, RogueInteger _value_1, RogueInteger _hash_2 );
RogueClassString_IntegerTableEntry* RogueString_IntegerTableEntry__init_object( RogueClassString_IntegerTableEntry* THIS );
RogueString* RogueString_IntegerTableEntryArray__type_name( RogueArray* THIS );
RogueString* RogueScope__type_name( RogueClassScope* THIS );
RogueClassScope* RogueScope__init( RogueClassScope* THIS, RogueClassType* _auto_519_0, RogueClassMethod* _auto_520_1 );
RogueClassLocal* RogueScope__find_local( RogueClassScope* THIS, RogueString* name_0 );
void RogueScope__push_local( RogueClassScope* THIS, RogueClassLocal* v_0, RogueLogical validate_name_1 );
void RogueScope__pop_local( RogueClassScope* THIS );
RogueClassCmd* RogueScope__resolve_call( RogueClassScope* THIS, RogueClassType* type_context_0, RogueClassCmdAccess* access_1, RogueLogical error_on_fail_2, RogueLogical suppress_inherited_3 );
RogueClassMethod* RogueScope__find_method( RogueClassScope* THIS, RogueClassType* type_context_0, RogueClassCmdAccess* access_1, RogueLogical error_on_fail_2, RogueLogical suppress_inherited_3 );
RogueClassScope* RogueScope__init_object( RogueClassScope* THIS );
RogueString* RogueCmdControlStructureList__to_String( RogueCmdControlStructureList* THIS );
RogueString* RogueCmdControlStructureList__type_name( RogueCmdControlStructureList* THIS );
RogueCmdControlStructureList* RogueCmdControlStructureList__init_object( RogueCmdControlStructureList* THIS );
RogueCmdControlStructureList* RogueCmdControlStructureList__init( RogueCmdControlStructureList* THIS );
RogueCmdControlStructureList* RogueCmdControlStructureList__init( RogueCmdControlStructureList* THIS, RogueInteger initial_capacity_0 );
RogueCmdControlStructureList* RogueCmdControlStructureList__init( RogueCmdControlStructureList* THIS, RogueInteger initial_capacity_0, RogueClassCmdControlStructure* initial_value_1 );
RogueCmdControlStructureList* RogueCmdControlStructureList__clone( RogueCmdControlStructureList* THIS );
RogueCmdControlStructureList* RogueCmdControlStructureList__add( RogueCmdControlStructureList* THIS, RogueClassCmdControlStructure* value_0 );
RogueCmdControlStructureList* RogueCmdControlStructureList__add( RogueCmdControlStructureList* THIS, RogueCmdControlStructureList* other_0 );
RogueInteger RogueCmdControlStructureList__capacity( RogueCmdControlStructureList* THIS );
RogueCmdControlStructureList* RogueCmdControlStructureList__clear( RogueCmdControlStructureList* THIS );
RogueCmdControlStructureList* RogueCmdControlStructureList__insert( RogueCmdControlStructureList* THIS, RogueClassCmdControlStructure* value_0, RogueInteger before_index_1 );
RogueClassCmdControlStructure* RogueCmdControlStructureList__last( RogueCmdControlStructureList* THIS );
RogueClassCmdControlStructure* RogueCmdControlStructureList__random( RogueCmdControlStructureList* THIS );
RogueCmdControlStructureList* RogueCmdControlStructureList__reserve( RogueCmdControlStructureList* THIS, RogueInteger additional_count_0 );
RogueClassCmdControlStructure* RogueCmdControlStructureList__remove( RogueCmdControlStructureList* THIS, RogueClassCmdControlStructure* value_0 );
RogueClassCmdControlStructure* RogueCmdControlStructureList__remove_at( RogueCmdControlStructureList* THIS, RogueInteger index_0 );
RogueClassCmdControlStructure* RogueCmdControlStructureList__remove_last( RogueCmdControlStructureList* THIS );
RogueString* RogueTaskArgs__type_name( RogueClassTaskArgs* THIS );
RogueClassTaskArgs* RogueTaskArgs__init_object( RogueClassTaskArgs* THIS );
RogueClassTaskArgs* RogueTaskArgs__init( RogueClassTaskArgs* THIS, RogueClassType* _auto_532_0, RogueClassMethod* _auto_533_1, RogueClassType* _auto_534_2, RogueClassMethod* _auto_535_3 );
RogueClassTaskArgs* RogueTaskArgs__add( RogueClassTaskArgs* THIS, RogueClassCmd* cmd_0 );
RogueClassTaskArgs* RogueTaskArgs__add_jump( RogueClassTaskArgs* THIS, RogueClassToken* t_0, RogueClassCmdTaskControlSection* to_section_1 );
RogueClassTaskArgs* RogueTaskArgs__add_conditional_jump( RogueClassTaskArgs* THIS, RogueClassCmd* condition_0, RogueClassCmdTaskControlSection* to_section_1 );
RogueClassCmd* RogueTaskArgs__create_return( RogueClassTaskArgs* THIS, RogueClassToken* t_0, RogueClassCmd* value_1 );
RogueClassCmd* RogueTaskArgs__create_escape( RogueClassTaskArgs* THIS, RogueClassToken* t_0, RogueClassCmdTaskControlSection* escape_section_1 );
RogueClassTaskArgs* RogueTaskArgs__add_yield( RogueClassTaskArgs* THIS, RogueClassToken* t_0 );
RogueClassCmdTaskControlSection* RogueTaskArgs__jump_to_new_section( RogueClassTaskArgs* THIS, RogueClassToken* t_0 );
RogueClassTaskArgs* RogueTaskArgs__begin_section( RogueClassTaskArgs* THIS, RogueClassCmdTaskControlSection* section_0 );
RogueLogical RogueTaskArgs__converting_task( RogueClassTaskArgs* THIS );
RogueClassCmdTaskControlSection* RogueTaskArgs__create_section( RogueClassTaskArgs* THIS );
RogueClassCmd* RogueTaskArgs__cmd_read_this( RogueClassTaskArgs* THIS, RogueClassToken* t_0 );
RogueClassCmd* RogueTaskArgs__cmd_read_context( RogueClassTaskArgs* THIS, RogueClassToken* t_0 );
RogueString* RogueTaskArgs__convert_local_name( RogueClassTaskArgs* THIS, RogueClassLocal* local_info_0 );
RogueClassCmd* RogueTaskArgs__cmd_read( RogueClassTaskArgs* THIS, RogueClassToken* t_0, RogueClassLocal* local_info_1 );
RogueClassCmd* RogueTaskArgs__cmd_write( RogueClassTaskArgs* THIS, RogueClassToken* t_0, RogueClassLocal* local_info_1, RogueClassCmd* new_value_2 );
RogueClassCmd* RogueTaskArgs__replace_write_local( RogueClassTaskArgs* THIS, RogueClassToken* t_0, RogueClassLocal* local_info_1, RogueClassCmd* new_value_2 );
RogueClassTaskArgs* RogueTaskArgs__set_next_ip( RogueClassTaskArgs* THIS, RogueClassToken* t_0, RogueClassCmdTaskControlSection* to_section_1 );
RogueClassCmd* RogueTaskArgs__create_set_next_ip( RogueClassTaskArgs* THIS, RogueClassToken* t_0, RogueClassCmdTaskControlSection* to_section_1 );
RogueString* RogueCmdArray__type_name( RogueArray* THIS );
RogueString* RogueCmdTaskControl__type_name( RogueClassCmdTaskControl* THIS );
void RogueCmdTaskControl__write_cpp( RogueClassCmdTaskControl* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueLogical RogueCmdTaskControl__requires_semicolon( RogueClassCmdTaskControl* THIS );
RogueClassCmd* RogueCmdTaskControl__resolve( RogueClassCmdTaskControl* THIS, RogueClassScope* scope_0 );
RogueClassCmdTaskControl* RogueCmdTaskControl__init_object( RogueClassCmdTaskControl* THIS );
RogueClassCmdTaskControl* RogueCmdTaskControl__init( RogueClassCmdTaskControl* THIS, RogueClassToken* _auto_538_0 );
RogueClassCmdTaskControl* RogueCmdTaskControl__add( RogueClassCmdTaskControl* THIS, RogueClassCmd* cmd_0 );
RogueString* RogueCmdTaskControlSection__type_name( RogueClassCmdTaskControlSection* THIS );
RogueClassCmdTaskControlSection* RogueCmdTaskControlSection__init( RogueClassCmdTaskControlSection* THIS, RogueInteger _auto_539_0 );
RogueClassCmdTaskControlSection* RogueCmdTaskControlSection__init_object( RogueClassCmdTaskControlSection* THIS );
RogueString* RogueCmdCastToType__type_name( RogueClassCmdCastToType* THIS );
RogueClassCmd* RogueCmdCastToType__clone( RogueClassCmdCastToType* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdCastToType__write_cpp( RogueClassCmdCastToType* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdCastToType__resolve( RogueClassCmdCastToType* THIS, RogueClassScope* scope_0 );
RogueClassCmdCastToType* RogueCmdCastToType__init_object( RogueClassCmdCastToType* THIS );
RogueString* RogueCmdCreateOptionalValue__type_name( RogueClassCmdCreateOptionalValue* THIS );
RogueClassCmd* RogueCmdCreateOptionalValue__clone( RogueClassCmdCreateOptionalValue* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdCreateOptionalValue__write_cpp( RogueClassCmdCreateOptionalValue* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdCreateOptionalValue__resolve( RogueClassCmdCreateOptionalValue* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdCreateOptionalValue__type( RogueClassCmdCreateOptionalValue* THIS );
RogueClassCmdCreateOptionalValue* RogueCmdCreateOptionalValue__init_object( RogueClassCmdCreateOptionalValue* THIS );
RogueClassCmdCreateOptionalValue* RogueCmdCreateOptionalValue__init( RogueClassCmdCreateOptionalValue* THIS, RogueClassToken* _auto_540_0, RogueClassType* _auto_541_1, RogueClassCmd* _auto_542_2 );
RogueString* RogueCmdListOps__type_name( RogueClassCmdListOps* THIS );
RogueClassCmdListOps* RogueCmdListOps__init_object( RogueClassCmdListOps* THIS );
RogueString* RogueString_MethodTableEntryList__to_String( RogueString_MethodTableEntryList* THIS );
RogueString* RogueString_MethodTableEntryList__type_name( RogueString_MethodTableEntryList* THIS );
RogueString_MethodTableEntryList* RogueString_MethodTableEntryList__init_object( RogueString_MethodTableEntryList* THIS );
RogueString_MethodTableEntryList* RogueString_MethodTableEntryList__init( RogueString_MethodTableEntryList* THIS );
RogueString_MethodTableEntryList* RogueString_MethodTableEntryList__init( RogueString_MethodTableEntryList* THIS, RogueInteger initial_capacity_0 );
RogueString_MethodTableEntryList* RogueString_MethodTableEntryList__init( RogueString_MethodTableEntryList* THIS, RogueInteger initial_capacity_0, RogueClassString_MethodTableEntry* initial_value_1 );
RogueString_MethodTableEntryList* RogueString_MethodTableEntryList__clone( RogueString_MethodTableEntryList* THIS );
RogueString_MethodTableEntryList* RogueString_MethodTableEntryList__add( RogueString_MethodTableEntryList* THIS, RogueClassString_MethodTableEntry* value_0 );
RogueString_MethodTableEntryList* RogueString_MethodTableEntryList__add( RogueString_MethodTableEntryList* THIS, RogueString_MethodTableEntryList* other_0 );
RogueInteger RogueString_MethodTableEntryList__capacity( RogueString_MethodTableEntryList* THIS );
RogueString_MethodTableEntryList* RogueString_MethodTableEntryList__clear( RogueString_MethodTableEntryList* THIS );
RogueString_MethodTableEntryList* RogueString_MethodTableEntryList__insert( RogueString_MethodTableEntryList* THIS, RogueClassString_MethodTableEntry* value_0, RogueInteger before_index_1 );
RogueClassString_MethodTableEntry* RogueString_MethodTableEntryList__last( RogueString_MethodTableEntryList* THIS );
RogueClassString_MethodTableEntry* RogueString_MethodTableEntryList__random( RogueString_MethodTableEntryList* THIS );
RogueString_MethodTableEntryList* RogueString_MethodTableEntryList__reserve( RogueString_MethodTableEntryList* THIS, RogueInteger additional_count_0 );
RogueClassString_MethodTableEntry* RogueString_MethodTableEntryList__remove( RogueString_MethodTableEntryList* THIS, RogueClassString_MethodTableEntry* value_0 );
RogueClassString_MethodTableEntry* RogueString_MethodTableEntryList__remove_at( RogueString_MethodTableEntryList* THIS, RogueInteger index_0 );
RogueClassString_MethodTableEntry* RogueString_MethodTableEntryList__remove_last( RogueString_MethodTableEntryList* THIS );
RogueString* RogueString_MethodTableEntry__type_name( RogueClassString_MethodTableEntry* THIS );
RogueClassString_MethodTableEntry* RogueString_MethodTableEntry__init( RogueClassString_MethodTableEntry* THIS, RogueString* _key_0, RogueClassMethod* _value_1, RogueInteger _hash_2 );
RogueClassString_MethodTableEntry* RogueString_MethodTableEntry__init_object( RogueClassString_MethodTableEntry* THIS );
RogueString* RogueString_MethodTableEntryArray__type_name( RogueArray* THIS );
RogueString* RogueLocalListOps__type_name( RogueClassLocalListOps* THIS );
RogueClassLocalListOps* RogueLocalListOps__init_object( RogueClassLocalListOps* THIS );
RogueString* RogueTaskListOps__type_name( RogueClassTaskListOps* THIS );
RogueClassTaskListOps* RogueTaskListOps__init_object( RogueClassTaskListOps* THIS );
RogueString* RogueTime__type_name( RogueClassTime* THIS );
RogueClassTime* RogueTime__init_object( RogueClassTime* THIS );
RogueString* RogueString_LogicalTableEntryListOps__type_name( RogueClassString_LogicalTableEntryListOps* THIS );
RogueClassString_LogicalTableEntryListOps* RogueString_LogicalTableEntryListOps__init_object( RogueClassString_LogicalTableEntryListOps* THIS );
RogueString* RogueByteArray__type_name( RogueArray* THIS );
RogueString* RogueByteListOps__type_name( RogueClassByteListOps* THIS );
RogueClassByteListOps* RogueByteListOps__init_object( RogueClassByteListOps* THIS );
RogueString* RogueDirectiveTokenType__type_name( RogueClassDirectiveTokenType* THIS );
RogueClassToken* RogueDirectiveTokenType__create_token( RogueClassDirectiveTokenType* THIS, RogueString* filepath_0, RogueInteger line_1, RogueInteger column_2 );
RogueLogical RogueDirectiveTokenType__is_directive( RogueClassDirectiveTokenType* THIS );
RogueClassDirectiveTokenType* RogueDirectiveTokenType__init_object( RogueClassDirectiveTokenType* THIS );
RogueString* RogueEOLTokenType__type_name( RogueClassEOLTokenType* THIS );
RogueClassToken* RogueEOLTokenType__create_token( RogueClassEOLTokenType* THIS, RogueString* filepath_0, RogueInteger line_1, RogueInteger column_2 );
RogueClassToken* RogueEOLTokenType__create_token( RogueClassEOLTokenType* THIS, RogueString* filepath_0, RogueInteger line_1, RogueInteger column_2, RogueString* value_3 );
RogueLogical RogueEOLTokenType__is_structure( RogueClassEOLTokenType* THIS );
RogueClassEOLTokenType* RogueEOLTokenType__init_object( RogueClassEOLTokenType* THIS );
RogueString* RogueStructureTokenType__type_name( RogueClassStructureTokenType* THIS );
RogueClassToken* RogueStructureTokenType__create_token( RogueClassStructureTokenType* THIS, RogueString* filepath_0, RogueInteger line_1, RogueInteger column_2 );
RogueLogical RogueStructureTokenType__is_structure( RogueClassStructureTokenType* THIS );
RogueClassStructureTokenType* RogueStructureTokenType__init_object( RogueClassStructureTokenType* THIS );
RogueString* RogueOpWithAssignTokenType__type_name( RogueClassOpWithAssignTokenType* THIS );
RogueLogical RogueOpWithAssignTokenType__is_op_with_assign( RogueClassOpWithAssignTokenType* THIS );
RogueClassOpWithAssignTokenType* RogueOpWithAssignTokenType__init_object( RogueClassOpWithAssignTokenType* THIS );
RogueString* RogueEOLToken__to_String( RogueClassEOLToken* THIS );
RogueString* RogueEOLToken__type_name( RogueClassEOLToken* THIS );
RogueClassEOLToken* RogueEOLToken__init_object( RogueClassEOLToken* THIS );
RogueClassEOLToken* RogueEOLToken__init( RogueClassEOLToken* THIS, RogueClassTokenType* _auto_604_0, RogueString* _auto_605_1 );
RogueString* RogueString_TokenListTable__to_String( RogueClassString_TokenListTable* THIS );
RogueString* RogueString_TokenListTable__type_name( RogueClassString_TokenListTable* THIS );
RogueClassString_TokenListTable* RogueString_TokenListTable__init( RogueClassString_TokenListTable* THIS );
RogueClassString_TokenListTable* RogueString_TokenListTable__init( RogueClassString_TokenListTable* THIS, RogueInteger bin_count_0 );
RogueClassString_TokenListTable* RogueString_TokenListTable__init( RogueClassString_TokenListTable* THIS, RogueClassString_TokenListTable* other_0 );
RogueClassString_TokenListTable* RogueString_TokenListTable__add( RogueClassString_TokenListTable* THIS, RogueClassString_TokenListTable* other_0 );
RogueTokenList* RogueString_TokenListTable__at( RogueClassString_TokenListTable* THIS, RogueInteger index_0 );
void RogueString_TokenListTable__clear( RogueClassString_TokenListTable* THIS );
RogueClassString_TokenListTable* RogueString_TokenListTable__clone( RogueClassString_TokenListTable* THIS );
RogueLogical RogueString_TokenListTable__contains( RogueClassString_TokenListTable* THIS, RogueString* key_0 );
RogueInteger RogueString_TokenListTable__count( RogueClassString_TokenListTable* THIS );
RogueClassString_TokenListTableEntry* RogueString_TokenListTable__find( RogueClassString_TokenListTable* THIS, RogueString* key_0 );
RogueTokenList* RogueString_TokenListTable__get( RogueClassString_TokenListTable* THIS, RogueString* key_0 );
RogueTokenList* RogueString_TokenListTable__remove( RogueClassString_TokenListTable* THIS, RogueString* key_0 );
void RogueString_TokenListTable__set( RogueClassString_TokenListTable* THIS, RogueString* key_0, RogueTokenList* value_1 );
RogueStringBuilder* RogueString_TokenListTable__print_to( RogueClassString_TokenListTable* THIS, RogueStringBuilder* buffer_0 );
RogueClassString_TokenListTable* RogueString_TokenListTable__init_object( RogueClassString_TokenListTable* THIS );
RogueString* RoguePreprocessorTokenReader__type_name( RogueClassPreprocessorTokenReader* THIS );
RogueClassPreprocessorTokenReader* RoguePreprocessorTokenReader__init( RogueClassPreprocessorTokenReader* THIS, RogueTokenList* _auto_614_0 );
RogueClassError* RoguePreprocessorTokenReader__error( RogueClassPreprocessorTokenReader* THIS, RogueString* message_0 );
void RoguePreprocessorTokenReader__expand_definition( RogueClassPreprocessorTokenReader* THIS, RogueClassToken* t_0 );
RogueLogical RoguePreprocessorTokenReader__has_another( RogueClassPreprocessorTokenReader* THIS );
RogueLogical RoguePreprocessorTokenReader__next_is( RogueClassPreprocessorTokenReader* THIS, RogueClassTokenType* type_0 );
RogueClassToken* RoguePreprocessorTokenReader__peek( RogueClassPreprocessorTokenReader* THIS );
RogueClassToken* RoguePreprocessorTokenReader__peek( RogueClassPreprocessorTokenReader* THIS, RogueInteger num_ahead_0 );
void RoguePreprocessorTokenReader__push( RogueClassPreprocessorTokenReader* THIS, RogueClassToken* t_0 );
void RoguePreprocessorTokenReader__push( RogueClassPreprocessorTokenReader* THIS, RogueTokenList* _tokens_0 );
RogueClassToken* RoguePreprocessorTokenReader__read( RogueClassPreprocessorTokenReader* THIS );
RogueString* RoguePreprocessorTokenReader__read_identifier( RogueClassPreprocessorTokenReader* THIS );
RogueClassPreprocessorTokenReader* RoguePreprocessorTokenReader__init_object( RogueClassPreprocessorTokenReader* THIS );
RogueString* RogueString_TokenTypeTableEntryList__to_String( RogueString_TokenTypeTableEntryList* THIS );
RogueString* RogueString_TokenTypeTableEntryList__type_name( RogueString_TokenTypeTableEntryList* THIS );
RogueString_TokenTypeTableEntryList* RogueString_TokenTypeTableEntryList__init_object( RogueString_TokenTypeTableEntryList* THIS );
RogueString_TokenTypeTableEntryList* RogueString_TokenTypeTableEntryList__init( RogueString_TokenTypeTableEntryList* THIS );
RogueString_TokenTypeTableEntryList* RogueString_TokenTypeTableEntryList__init( RogueString_TokenTypeTableEntryList* THIS, RogueInteger initial_capacity_0 );
RogueString_TokenTypeTableEntryList* RogueString_TokenTypeTableEntryList__init( RogueString_TokenTypeTableEntryList* THIS, RogueInteger initial_capacity_0, RogueClassString_TokenTypeTableEntry* initial_value_1 );
RogueString_TokenTypeTableEntryList* RogueString_TokenTypeTableEntryList__clone( RogueString_TokenTypeTableEntryList* THIS );
RogueString_TokenTypeTableEntryList* RogueString_TokenTypeTableEntryList__add( RogueString_TokenTypeTableEntryList* THIS, RogueClassString_TokenTypeTableEntry* value_0 );
RogueString_TokenTypeTableEntryList* RogueString_TokenTypeTableEntryList__add( RogueString_TokenTypeTableEntryList* THIS, RogueString_TokenTypeTableEntryList* other_0 );
RogueInteger RogueString_TokenTypeTableEntryList__capacity( RogueString_TokenTypeTableEntryList* THIS );
RogueString_TokenTypeTableEntryList* RogueString_TokenTypeTableEntryList__clear( RogueString_TokenTypeTableEntryList* THIS );
RogueString_TokenTypeTableEntryList* RogueString_TokenTypeTableEntryList__insert( RogueString_TokenTypeTableEntryList* THIS, RogueClassString_TokenTypeTableEntry* value_0, RogueInteger before_index_1 );
RogueClassString_TokenTypeTableEntry* RogueString_TokenTypeTableEntryList__last( RogueString_TokenTypeTableEntryList* THIS );
RogueClassString_TokenTypeTableEntry* RogueString_TokenTypeTableEntryList__random( RogueString_TokenTypeTableEntryList* THIS );
RogueString_TokenTypeTableEntryList* RogueString_TokenTypeTableEntryList__reserve( RogueString_TokenTypeTableEntryList* THIS, RogueInteger additional_count_0 );
RogueClassString_TokenTypeTableEntry* RogueString_TokenTypeTableEntryList__remove( RogueString_TokenTypeTableEntryList* THIS, RogueClassString_TokenTypeTableEntry* value_0 );
RogueClassString_TokenTypeTableEntry* RogueString_TokenTypeTableEntryList__remove_at( RogueString_TokenTypeTableEntryList* THIS, RogueInteger index_0 );
RogueClassString_TokenTypeTableEntry* RogueString_TokenTypeTableEntryList__remove_last( RogueString_TokenTypeTableEntryList* THIS );
RogueString* RogueString_TokenTypeTableEntry__type_name( RogueClassString_TokenTypeTableEntry* THIS );
RogueClassString_TokenTypeTableEntry* RogueString_TokenTypeTableEntry__init( RogueClassString_TokenTypeTableEntry* THIS, RogueString* _key_0, RogueClassTokenType* _value_1, RogueInteger _hash_2 );
RogueClassString_TokenTypeTableEntry* RogueString_TokenTypeTableEntry__init_object( RogueClassString_TokenTypeTableEntry* THIS );
RogueString* RogueString_TokenTypeTableEntryArray__type_name( RogueArray* THIS );
RogueString* RogueString_CmdTableEntryList__to_String( RogueString_CmdTableEntryList* THIS );
RogueString* RogueString_CmdTableEntryList__type_name( RogueString_CmdTableEntryList* THIS );
RogueString_CmdTableEntryList* RogueString_CmdTableEntryList__init_object( RogueString_CmdTableEntryList* THIS );
RogueString_CmdTableEntryList* RogueString_CmdTableEntryList__init( RogueString_CmdTableEntryList* THIS );
RogueString_CmdTableEntryList* RogueString_CmdTableEntryList__init( RogueString_CmdTableEntryList* THIS, RogueInteger initial_capacity_0 );
RogueString_CmdTableEntryList* RogueString_CmdTableEntryList__init( RogueString_CmdTableEntryList* THIS, RogueInteger initial_capacity_0, RogueClassString_CmdTableEntry* initial_value_1 );
RogueString_CmdTableEntryList* RogueString_CmdTableEntryList__clone( RogueString_CmdTableEntryList* THIS );
RogueString_CmdTableEntryList* RogueString_CmdTableEntryList__add( RogueString_CmdTableEntryList* THIS, RogueClassString_CmdTableEntry* value_0 );
RogueString_CmdTableEntryList* RogueString_CmdTableEntryList__add( RogueString_CmdTableEntryList* THIS, RogueString_CmdTableEntryList* other_0 );
RogueInteger RogueString_CmdTableEntryList__capacity( RogueString_CmdTableEntryList* THIS );
RogueString_CmdTableEntryList* RogueString_CmdTableEntryList__clear( RogueString_CmdTableEntryList* THIS );
RogueString_CmdTableEntryList* RogueString_CmdTableEntryList__insert( RogueString_CmdTableEntryList* THIS, RogueClassString_CmdTableEntry* value_0, RogueInteger before_index_1 );
RogueClassString_CmdTableEntry* RogueString_CmdTableEntryList__last( RogueString_CmdTableEntryList* THIS );
RogueClassString_CmdTableEntry* RogueString_CmdTableEntryList__random( RogueString_CmdTableEntryList* THIS );
RogueString_CmdTableEntryList* RogueString_CmdTableEntryList__reserve( RogueString_CmdTableEntryList* THIS, RogueInteger additional_count_0 );
RogueClassString_CmdTableEntry* RogueString_CmdTableEntryList__remove( RogueString_CmdTableEntryList* THIS, RogueClassString_CmdTableEntry* value_0 );
RogueClassString_CmdTableEntry* RogueString_CmdTableEntryList__remove_at( RogueString_CmdTableEntryList* THIS, RogueInteger index_0 );
RogueClassString_CmdTableEntry* RogueString_CmdTableEntryList__remove_last( RogueString_CmdTableEntryList* THIS );
RogueString* RogueString_CmdTableEntry__type_name( RogueClassString_CmdTableEntry* THIS );
RogueClassString_CmdTableEntry* RogueString_CmdTableEntry__init( RogueClassString_CmdTableEntry* THIS, RogueString* _key_0, RogueClassCmd* _value_1, RogueInteger _hash_2 );
RogueClassString_CmdTableEntry* RogueString_CmdTableEntry__init_object( RogueClassString_CmdTableEntry* THIS );
RogueString* RogueString_CmdTableEntryArray__type_name( RogueArray* THIS );
RogueString* RogueCmdTaskControlSectionList__to_String( RogueCmdTaskControlSectionList* THIS );
RogueString* RogueCmdTaskControlSectionList__type_name( RogueCmdTaskControlSectionList* THIS );
RogueCmdTaskControlSectionList* RogueCmdTaskControlSectionList__init_object( RogueCmdTaskControlSectionList* THIS );
RogueCmdTaskControlSectionList* RogueCmdTaskControlSectionList__init( RogueCmdTaskControlSectionList* THIS );
RogueCmdTaskControlSectionList* RogueCmdTaskControlSectionList__init( RogueCmdTaskControlSectionList* THIS, RogueInteger initial_capacity_0 );
RogueCmdTaskControlSectionList* RogueCmdTaskControlSectionList__init( RogueCmdTaskControlSectionList* THIS, RogueInteger initial_capacity_0, RogueClassCmdTaskControlSection* initial_value_1 );
RogueCmdTaskControlSectionList* RogueCmdTaskControlSectionList__clone( RogueCmdTaskControlSectionList* THIS );
RogueCmdTaskControlSectionList* RogueCmdTaskControlSectionList__add( RogueCmdTaskControlSectionList* THIS, RogueClassCmdTaskControlSection* value_0 );
RogueCmdTaskControlSectionList* RogueCmdTaskControlSectionList__add( RogueCmdTaskControlSectionList* THIS, RogueCmdTaskControlSectionList* other_0 );
RogueInteger RogueCmdTaskControlSectionList__capacity( RogueCmdTaskControlSectionList* THIS );
RogueCmdTaskControlSectionList* RogueCmdTaskControlSectionList__clear( RogueCmdTaskControlSectionList* THIS );
RogueCmdTaskControlSectionList* RogueCmdTaskControlSectionList__insert( RogueCmdTaskControlSectionList* THIS, RogueClassCmdTaskControlSection* value_0, RogueInteger before_index_1 );
RogueClassCmdTaskControlSection* RogueCmdTaskControlSectionList__last( RogueCmdTaskControlSectionList* THIS );
RogueClassCmdTaskControlSection* RogueCmdTaskControlSectionList__random( RogueCmdTaskControlSectionList* THIS );
RogueCmdTaskControlSectionList* RogueCmdTaskControlSectionList__reserve( RogueCmdTaskControlSectionList* THIS, RogueInteger additional_count_0 );
RogueClassCmdTaskControlSection* RogueCmdTaskControlSectionList__remove( RogueCmdTaskControlSectionList* THIS, RogueClassCmdTaskControlSection* value_0 );
RogueClassCmdTaskControlSection* RogueCmdTaskControlSectionList__remove_at( RogueCmdTaskControlSectionList* THIS, RogueInteger index_0 );
RogueClassCmdTaskControlSection* RogueCmdTaskControlSectionList__remove_last( RogueCmdTaskControlSectionList* THIS );
RogueString* RogueCmdWhichCaseArray__type_name( RogueArray* THIS );
RogueString* RogueCmdBlock__type_name( RogueClassCmdBlock* THIS );
RogueClassCmd* RogueCmdBlock__clone( RogueClassCmdBlock* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdBlock__write_cpp( RogueClassCmdBlock* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueLogical RogueCmdBlock__requires_semicolon( RogueClassCmdBlock* THIS );
RogueClassCmdBlock* RogueCmdBlock__resolve( RogueClassCmdBlock* THIS, RogueClassScope* scope_0 );
RogueClassCmdBlock* RogueCmdBlock__init_object( RogueClassCmdBlock* THIS );
RogueClassCmdBlock* RogueCmdBlock__init( RogueClassCmdBlock* THIS, RogueClassToken* _auto_637_0, RogueInteger _auto_638_1 );
RogueClassCmdBlock* RogueCmdBlock__init( RogueClassCmdBlock* THIS, RogueClassToken* _auto_639_0, RogueClassCmdStatementList* _auto_640_1, RogueInteger _auto_641_2 );
RogueClassCmdBlock* RogueCmdBlock__init( RogueClassCmdBlock* THIS, RogueClassCmd* cmd_0 );
RogueString* RogueCmdCatchArray__type_name( RogueArray* THIS );
RogueString* RogueCmdReadProperty__type_name( RogueClassCmdReadProperty* THIS );
RogueString* RogueCmdReadProperty__access_name( RogueClassCmdReadProperty* THIS );
RogueString* RogueCmdReadProperty__access_cpp_name( RogueClassCmdReadProperty* THIS );
RogueClassCmd* RogueCmdReadProperty__clone( RogueClassCmdReadProperty* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdReadProperty__write_cpp( RogueClassCmdReadProperty* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdReadProperty__resolve( RogueClassCmdReadProperty* THIS, RogueClassScope* scope_0 );
RogueClassCmd* RogueCmdReadProperty__resolve_modify( RogueClassCmdReadProperty* THIS, RogueClassScope* scope_0, RogueInteger delta_1 );
RogueClassType* Rogue_CmdReadProperty__type( RogueClassCmdReadProperty* THIS );
RogueClassCmdReadProperty* RogueCmdReadProperty__init_object( RogueClassCmdReadProperty* THIS );
RogueClassCmdReadProperty* RogueCmdReadProperty__init( RogueClassCmdReadProperty* THIS, RogueClassToken* _auto_670_0, RogueClassCmd* _auto_671_1, RogueClassProperty* _auto_672_2 );
RogueString* RogueCmdWriteLocal__type_name( RogueClassCmdWriteLocal* THIS );
RogueClassCmd* RogueCmdWriteLocal__clone( RogueClassCmdWriteLocal* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdWriteLocal__write_cpp( RogueClassCmdWriteLocal* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdWriteLocal__resolve( RogueClassCmdWriteLocal* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdWriteLocal__type( RogueClassCmdWriteLocal* THIS );
RogueClassCmdWriteLocal* RogueCmdWriteLocal__init_object( RogueClassCmdWriteLocal* THIS );
RogueClassCmdWriteLocal* RogueCmdWriteLocal__init( RogueClassCmdWriteLocal* THIS, RogueClassToken* _auto_673_0, RogueClassLocal* _auto_674_1, RogueClassCmd* _auto_675_2 );
RogueString* RogueCmdControlStructureArray__type_name( RogueArray* THIS );
RogueString* RogueInlineArgs__type_name( RogueClassInlineArgs* THIS );
RogueClassInlineArgs* RogueInlineArgs__init_object( RogueClassInlineArgs* THIS );
RogueClassInlineArgs* RogueInlineArgs__init( RogueClassInlineArgs* THIS, RogueClassCmd* _auto_678_0, RogueClassMethod* _auto_679_1, RogueClassCmdArgs* args_2 );
RogueLogical RogueInlineArgs__inlining( RogueClassInlineArgs* THIS );
RogueClassCmd* RogueInlineArgs__inline_this( RogueClassInlineArgs* THIS );
RogueClassCmd* RogueInlineArgs__inline_access( RogueClassInlineArgs* THIS, RogueClassCmdAccess* access_0 );
RogueClassCmd* RogueInlineArgs__inline_read_local( RogueClassInlineArgs* THIS, RogueClassCmdReadLocal* read_cmd_0 );
RogueClassCmd* RogueInlineArgs__inline_write_local( RogueClassInlineArgs* THIS, RogueClassCmdWriteLocal* write_cmd_0 );
RogueString* RogueCmdReadSingleton__type_name( RogueClassCmdReadSingleton* THIS );
RogueString* RogueCmdReadSingleton__access_name( RogueClassCmdReadSingleton* THIS );
RogueString* RogueCmdReadSingleton__access_cpp_name( RogueClassCmdReadSingleton* THIS );
RogueClassCmd* RogueCmdReadSingleton__clone( RogueClassCmdReadSingleton* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdReadSingleton__write_cpp( RogueClassCmdReadSingleton* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
void RogueCmdReadSingleton__require_type_context( RogueClassCmdReadSingleton* THIS );
RogueClassCmd* RogueCmdReadSingleton__resolve( RogueClassCmdReadSingleton* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdReadSingleton__type( RogueClassCmdReadSingleton* THIS );
RogueClassCmdReadSingleton* RogueCmdReadSingleton__init_object( RogueClassCmdReadSingleton* THIS );
RogueClassCmdReadSingleton* RogueCmdReadSingleton__init( RogueClassCmdReadSingleton* THIS, RogueClassToken* _auto_680_0, RogueClassType* _auto_681_1 );
RogueString* RogueCmdCreateArray__type_name( RogueClassCmdCreateArray* THIS );
RogueClassCmd* RogueCmdCreateArray__clone( RogueClassCmdCreateArray* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdCreateArray__write_cpp( RogueClassCmdCreateArray* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdCreateArray__resolve( RogueClassCmdCreateArray* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdCreateArray__type( RogueClassCmdCreateArray* THIS );
RogueClassCmdCreateArray* RogueCmdCreateArray__init_object( RogueClassCmdCreateArray* THIS );
RogueClassCmdCreateArray* RogueCmdCreateArray__init( RogueClassCmdCreateArray* THIS, RogueClassToken* _auto_682_0, RogueClassType* _auto_683_1, RogueClassCmdArgs* args_2 );
RogueClassCmdCreateArray* RogueCmdCreateArray__init( RogueClassCmdCreateArray* THIS, RogueClassToken* _auto_684_0, RogueClassType* _auto_685_1, RogueClassCmd* _auto_686_2 );
RogueString* RogueCmdCallRoutine__type_name( RogueClassCmdCallRoutine* THIS );
RogueClassCmd* RogueCmdCallRoutine__clone( RogueClassCmdCallRoutine* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdCallRoutine__write_cpp( RogueClassCmdCallRoutine* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmdCallRoutine* RogueCmdCallRoutine__init_object( RogueClassCmdCallRoutine* THIS );
RogueClassCmdCallRoutine* RogueCmdCallRoutine__init( RogueClassCmdCallRoutine* THIS, RogueClassToken* _auto_691_0, RogueClassMethod* _auto_692_1, RogueClassCmdArgs* _auto_693_2 );
RogueString* RogueCmdCall__type_name( RogueClassCmdCall* THIS );
RogueClassCmd* RogueCmdCall__resolve( RogueClassCmdCall* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdCall__type( RogueClassCmdCall* THIS );
RogueClassCmdCall* RogueCmdCall__init_object( RogueClassCmdCall* THIS );
RogueClassCmdCall* RogueCmdCall__init( RogueClassCmdCall* THIS, RogueClassToken* _auto_687_0, RogueClassCmd* _auto_688_1, RogueClassMethod* _auto_689_2, RogueClassCmdArgs* _auto_690_3 );
RogueString* RogueCmdCreateObject__type_name( RogueClassCmdCreateObject* THIS );
RogueClassCmd* RogueCmdCreateObject__clone( RogueClassCmdCreateObject* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdCreateObject__write_cpp( RogueClassCmdCreateObject* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdCreateObject__resolve( RogueClassCmdCreateObject* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdCreateObject__type( RogueClassCmdCreateObject* THIS );
RogueClassCmdCreateObject* RogueCmdCreateObject__init_object( RogueClassCmdCreateObject* THIS );
RogueClassCmdCreateObject* RogueCmdCreateObject__init( RogueClassCmdCreateObject* THIS, RogueClassToken* _auto_694_0, RogueClassType* _auto_695_1 );
RogueString* RogueCmdReadSetting__type_name( RogueClassCmdReadSetting* THIS );
RogueString* RogueCmdReadSetting__access_name( RogueClassCmdReadSetting* THIS );
RogueString* RogueCmdReadSetting__access_cpp_name( RogueClassCmdReadSetting* THIS );
RogueClassCmd* RogueCmdReadSetting__clone( RogueClassCmdReadSetting* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdReadSetting__write_cpp( RogueClassCmdReadSetting* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdReadSetting__resolve( RogueClassCmdReadSetting* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdReadSetting__type( RogueClassCmdReadSetting* THIS );
RogueClassCmdReadSetting* RogueCmdReadSetting__init_object( RogueClassCmdReadSetting* THIS );
RogueClassCmdReadSetting* RogueCmdReadSetting__init( RogueClassCmdReadSetting* THIS, RogueClassToken* _auto_696_0, RogueClassProperty* _auto_697_1 );
RogueString* RogueCmdLogicalizeOptionalValue__type_name( RogueClassCmdLogicalizeOptionalValue* THIS );
RogueString* RogueCmdLogicalizeOptionalValue__access_name( RogueClassCmdLogicalizeOptionalValue* THIS );
RogueString* RogueCmdLogicalizeOptionalValue__access_cpp_name( RogueClassCmdLogicalizeOptionalValue* THIS );
RogueClassCmd* RogueCmdLogicalizeOptionalValue__clone( RogueClassCmdLogicalizeOptionalValue* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdLogicalizeOptionalValue__write_cpp( RogueClassCmdLogicalizeOptionalValue* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmdLogicalizeOptionalValue* RogueCmdLogicalizeOptionalValue__resolve( RogueClassCmdLogicalizeOptionalValue* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdLogicalizeOptionalValue__type( RogueClassCmdLogicalizeOptionalValue* THIS );
RogueClassCmdLogicalizeOptionalValue* RogueCmdLogicalizeOptionalValue__init_object( RogueClassCmdLogicalizeOptionalValue* THIS );
RogueClassCmdLogicalizeOptionalValue* RogueCmdLogicalizeOptionalValue__init( RogueClassCmdLogicalizeOptionalValue* THIS, RogueClassToken* _auto_699_0, RogueClassCmd* _auto_700_1, RogueLogical _auto_701_2 );
RogueString* RogueCmdOpAssignSetting__type_name( RogueClassCmdOpAssignSetting* THIS );
RogueClassCmd* RogueCmdOpAssignSetting__clone( RogueClassCmdOpAssignSetting* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdOpAssignSetting__write_cpp( RogueClassCmdOpAssignSetting* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdOpAssignSetting__resolve( RogueClassCmdOpAssignSetting* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdOpAssignSetting__type( RogueClassCmdOpAssignSetting* THIS );
RogueClassCmdOpAssignSetting* RogueCmdOpAssignSetting__init_object( RogueClassCmdOpAssignSetting* THIS );
RogueClassCmdOpAssignSetting* RogueCmdOpAssignSetting__init( RogueClassCmdOpAssignSetting* THIS, RogueClassToken* _auto_702_0, RogueClassProperty* _auto_703_1, RogueClassTokenType* _auto_704_2, RogueClassCmd* _auto_705_3 );
RogueString* RogueCmdOpAssignProperty__type_name( RogueClassCmdOpAssignProperty* THIS );
RogueClassCmd* RogueCmdOpAssignProperty__clone( RogueClassCmdOpAssignProperty* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdOpAssignProperty__write_cpp( RogueClassCmdOpAssignProperty* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdOpAssignProperty__resolve( RogueClassCmdOpAssignProperty* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdOpAssignProperty__type( RogueClassCmdOpAssignProperty* THIS );
RogueClassCmdOpAssignProperty* RogueCmdOpAssignProperty__init_object( RogueClassCmdOpAssignProperty* THIS );
RogueClassCmdOpAssignProperty* RogueCmdOpAssignProperty__init( RogueClassCmdOpAssignProperty* THIS, RogueClassToken* _auto_706_0, RogueClassCmd* _auto_707_1, RogueClassProperty* _auto_708_2, RogueClassTokenType* _auto_709_3, RogueClassCmd* _auto_710_4 );
RogueString* RogueCmdWhichCaseListOps__type_name( RogueClassCmdWhichCaseListOps* THIS );
RogueClassCmdWhichCaseListOps* RogueCmdWhichCaseListOps__init_object( RogueClassCmdWhichCaseListOps* THIS );
RogueString* RogueCmdCatchListOps__type_name( RogueClassCmdCatchListOps* THIS );
RogueClassCmdCatchListOps* RogueCmdCatchListOps__init_object( RogueClassCmdCatchListOps* THIS );
RogueString* RogueCmdReadArrayElement__type_name( RogueClassCmdReadArrayElement* THIS );
RogueClassCmd* RogueCmdReadArrayElement__clone( RogueClassCmdReadArrayElement* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdReadArrayElement__write_cpp( RogueClassCmdReadArrayElement* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdReadArrayElement__resolve( RogueClassCmdReadArrayElement* THIS, RogueClassScope* scope_0 );
RogueClassCmd* RogueCmdReadArrayElement__resolve_modify( RogueClassCmdReadArrayElement* THIS, RogueClassScope* scope_0, RogueInteger delta_1 );
RogueClassType* Rogue_CmdReadArrayElement__type( RogueClassCmdReadArrayElement* THIS );
RogueClassCmdReadArrayElement* RogueCmdReadArrayElement__init_object( RogueClassCmdReadArrayElement* THIS );
RogueClassCmdReadArrayElement* RogueCmdReadArrayElement__init( RogueClassCmdReadArrayElement* THIS, RogueClassToken* _auto_726_0, RogueClassCmd* _auto_727_1, RogueClassCmd* _auto_728_2 );
RogueString* RogueCmdWriteArrayElement__type_name( RogueClassCmdWriteArrayElement* THIS );
RogueClassCmd* RogueCmdWriteArrayElement__clone( RogueClassCmdWriteArrayElement* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdWriteArrayElement__write_cpp( RogueClassCmdWriteArrayElement* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdWriteArrayElement__resolve( RogueClassCmdWriteArrayElement* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdWriteArrayElement__type( RogueClassCmdWriteArrayElement* THIS );
RogueClassCmdWriteArrayElement* RogueCmdWriteArrayElement__init_object( RogueClassCmdWriteArrayElement* THIS );
RogueClassCmdWriteArrayElement* RogueCmdWriteArrayElement__init( RogueClassCmdWriteArrayElement* THIS, RogueClassToken* _auto_729_0, RogueClassCmd* _auto_730_1, RogueClassCmd* _auto_731_2, RogueClassCmd* _auto_732_3 );
RogueString* RogueCmdConvertToPrimitiveType__type_name( RogueClassCmdConvertToPrimitiveType* THIS );
RogueClassCmd* RogueCmdConvertToPrimitiveType__clone( RogueClassCmdConvertToPrimitiveType* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdConvertToPrimitiveType__write_cpp( RogueClassCmdConvertToPrimitiveType* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdConvertToPrimitiveType__resolve( RogueClassCmdConvertToPrimitiveType* THIS, RogueClassScope* scope_0 );
RogueClassCmdConvertToPrimitiveType* RogueCmdConvertToPrimitiveType__init_object( RogueClassCmdConvertToPrimitiveType* THIS );
RogueString* RogueFnParamArray__type_name( RogueArray* THIS );
RogueString* RogueFnParamListOps__type_name( RogueClassFnParamListOps* THIS );
RogueClassFnParamListOps* RogueFnParamListOps__init_object( RogueClassFnParamListOps* THIS );
RogueString* RogueFnArgArray__type_name( RogueArray* THIS );
RogueString* RogueFnArgListOps__type_name( RogueClassFnArgListOps* THIS );
RogueClassFnArgListOps* RogueFnArgListOps__init_object( RogueClassFnArgListOps* THIS );
RogueString* RogueTypeParameterListOps__type_name( RogueClassTypeParameterListOps* THIS );
RogueClassTypeParameterListOps* RogueTypeParameterListOps__init_object( RogueClassTypeParameterListOps* THIS );
RogueString* RogueString_TypeSpecializerTableEntryList__to_String( RogueString_TypeSpecializerTableEntryList* THIS );
RogueString* RogueString_TypeSpecializerTableEntryList__type_name( RogueString_TypeSpecializerTableEntryList* THIS );
RogueString_TypeSpecializerTableEntryList* RogueString_TypeSpecializerTableEntryList__init_object( RogueString_TypeSpecializerTableEntryList* THIS );
RogueString_TypeSpecializerTableEntryList* RogueString_TypeSpecializerTableEntryList__init( RogueString_TypeSpecializerTableEntryList* THIS );
RogueString_TypeSpecializerTableEntryList* RogueString_TypeSpecializerTableEntryList__init( RogueString_TypeSpecializerTableEntryList* THIS, RogueInteger initial_capacity_0 );
RogueString_TypeSpecializerTableEntryList* RogueString_TypeSpecializerTableEntryList__init( RogueString_TypeSpecializerTableEntryList* THIS, RogueInteger initial_capacity_0, RogueClassString_TypeSpecializerTableEntry* initial_value_1 );
RogueString_TypeSpecializerTableEntryList* RogueString_TypeSpecializerTableEntryList__clone( RogueString_TypeSpecializerTableEntryList* THIS );
RogueString_TypeSpecializerTableEntryList* RogueString_TypeSpecializerTableEntryList__add( RogueString_TypeSpecializerTableEntryList* THIS, RogueClassString_TypeSpecializerTableEntry* value_0 );
RogueString_TypeSpecializerTableEntryList* RogueString_TypeSpecializerTableEntryList__add( RogueString_TypeSpecializerTableEntryList* THIS, RogueString_TypeSpecializerTableEntryList* other_0 );
RogueInteger RogueString_TypeSpecializerTableEntryList__capacity( RogueString_TypeSpecializerTableEntryList* THIS );
RogueString_TypeSpecializerTableEntryList* RogueString_TypeSpecializerTableEntryList__clear( RogueString_TypeSpecializerTableEntryList* THIS );
RogueString_TypeSpecializerTableEntryList* RogueString_TypeSpecializerTableEntryList__insert( RogueString_TypeSpecializerTableEntryList* THIS, RogueClassString_TypeSpecializerTableEntry* value_0, RogueInteger before_index_1 );
RogueClassString_TypeSpecializerTableEntry* RogueString_TypeSpecializerTableEntryList__last( RogueString_TypeSpecializerTableEntryList* THIS );
RogueClassString_TypeSpecializerTableEntry* RogueString_TypeSpecializerTableEntryList__random( RogueString_TypeSpecializerTableEntryList* THIS );
RogueString_TypeSpecializerTableEntryList* RogueString_TypeSpecializerTableEntryList__reserve( RogueString_TypeSpecializerTableEntryList* THIS, RogueInteger additional_count_0 );
RogueClassString_TypeSpecializerTableEntry* RogueString_TypeSpecializerTableEntryList__remove( RogueString_TypeSpecializerTableEntryList* THIS, RogueClassString_TypeSpecializerTableEntry* value_0 );
RogueClassString_TypeSpecializerTableEntry* RogueString_TypeSpecializerTableEntryList__remove_at( RogueString_TypeSpecializerTableEntryList* THIS, RogueInteger index_0 );
RogueClassString_TypeSpecializerTableEntry* RogueString_TypeSpecializerTableEntryList__remove_last( RogueString_TypeSpecializerTableEntryList* THIS );
RogueString* RogueString_TypeSpecializerTableEntry__type_name( RogueClassString_TypeSpecializerTableEntry* THIS );
RogueClassString_TypeSpecializerTableEntry* RogueString_TypeSpecializerTableEntry__init( RogueClassString_TypeSpecializerTableEntry* THIS, RogueString* _key_0, RogueClassTypeSpecializer* _value_1, RogueInteger _hash_2 );
RogueClassString_TypeSpecializerTableEntry* RogueString_TypeSpecializerTableEntry__init_object( RogueClassString_TypeSpecializerTableEntry* THIS );
RogueString* RogueString_TypeSpecializerTableEntryArray__type_name( RogueArray* THIS );
RogueString* RogueString_TemplateTableEntryListOps__type_name( RogueClassString_TemplateTableEntryListOps* THIS );
RogueClassString_TemplateTableEntryListOps* RogueString_TemplateTableEntryListOps__init_object( RogueClassString_TemplateTableEntryListOps* THIS );
RogueString* RogueString_PropertyTableEntryList__to_String( RogueString_PropertyTableEntryList* THIS );
RogueString* RogueString_PropertyTableEntryList__type_name( RogueString_PropertyTableEntryList* THIS );
RogueString_PropertyTableEntryList* RogueString_PropertyTableEntryList__init_object( RogueString_PropertyTableEntryList* THIS );
RogueString_PropertyTableEntryList* RogueString_PropertyTableEntryList__init( RogueString_PropertyTableEntryList* THIS );
RogueString_PropertyTableEntryList* RogueString_PropertyTableEntryList__init( RogueString_PropertyTableEntryList* THIS, RogueInteger initial_capacity_0 );
RogueString_PropertyTableEntryList* RogueString_PropertyTableEntryList__init( RogueString_PropertyTableEntryList* THIS, RogueInteger initial_capacity_0, RogueClassString_PropertyTableEntry* initial_value_1 );
RogueString_PropertyTableEntryList* RogueString_PropertyTableEntryList__clone( RogueString_PropertyTableEntryList* THIS );
RogueString_PropertyTableEntryList* RogueString_PropertyTableEntryList__add( RogueString_PropertyTableEntryList* THIS, RogueClassString_PropertyTableEntry* value_0 );
RogueString_PropertyTableEntryList* RogueString_PropertyTableEntryList__add( RogueString_PropertyTableEntryList* THIS, RogueString_PropertyTableEntryList* other_0 );
RogueInteger RogueString_PropertyTableEntryList__capacity( RogueString_PropertyTableEntryList* THIS );
RogueString_PropertyTableEntryList* RogueString_PropertyTableEntryList__clear( RogueString_PropertyTableEntryList* THIS );
RogueString_PropertyTableEntryList* RogueString_PropertyTableEntryList__insert( RogueString_PropertyTableEntryList* THIS, RogueClassString_PropertyTableEntry* value_0, RogueInteger before_index_1 );
RogueClassString_PropertyTableEntry* RogueString_PropertyTableEntryList__last( RogueString_PropertyTableEntryList* THIS );
RogueClassString_PropertyTableEntry* RogueString_PropertyTableEntryList__random( RogueString_PropertyTableEntryList* THIS );
RogueString_PropertyTableEntryList* RogueString_PropertyTableEntryList__reserve( RogueString_PropertyTableEntryList* THIS, RogueInteger additional_count_0 );
RogueClassString_PropertyTableEntry* RogueString_PropertyTableEntryList__remove( RogueString_PropertyTableEntryList* THIS, RogueClassString_PropertyTableEntry* value_0 );
RogueClassString_PropertyTableEntry* RogueString_PropertyTableEntryList__remove_at( RogueString_PropertyTableEntryList* THIS, RogueInteger index_0 );
RogueClassString_PropertyTableEntry* RogueString_PropertyTableEntryList__remove_last( RogueString_PropertyTableEntryList* THIS );
RogueString* RogueString_PropertyTableEntry__type_name( RogueClassString_PropertyTableEntry* THIS );
RogueClassString_PropertyTableEntry* RogueString_PropertyTableEntry__init( RogueClassString_PropertyTableEntry* THIS, RogueString* _key_0, RogueClassProperty* _value_1, RogueInteger _hash_2 );
RogueClassString_PropertyTableEntry* RogueString_PropertyTableEntry__init_object( RogueClassString_PropertyTableEntry* THIS );
RogueString* RogueString_PropertyTableEntryArray__type_name( RogueArray* THIS );
RogueString* RogueString_MethodListTableEntryList__to_String( RogueString_MethodListTableEntryList* THIS );
RogueString* RogueString_MethodListTableEntryList__type_name( RogueString_MethodListTableEntryList* THIS );
RogueString_MethodListTableEntryList* RogueString_MethodListTableEntryList__init_object( RogueString_MethodListTableEntryList* THIS );
RogueString_MethodListTableEntryList* RogueString_MethodListTableEntryList__init( RogueString_MethodListTableEntryList* THIS );
RogueString_MethodListTableEntryList* RogueString_MethodListTableEntryList__init( RogueString_MethodListTableEntryList* THIS, RogueInteger initial_capacity_0 );
RogueString_MethodListTableEntryList* RogueString_MethodListTableEntryList__init( RogueString_MethodListTableEntryList* THIS, RogueInteger initial_capacity_0, RogueClassString_MethodListTableEntry* initial_value_1 );
RogueString_MethodListTableEntryList* RogueString_MethodListTableEntryList__clone( RogueString_MethodListTableEntryList* THIS );
RogueString_MethodListTableEntryList* RogueString_MethodListTableEntryList__add( RogueString_MethodListTableEntryList* THIS, RogueClassString_MethodListTableEntry* value_0 );
RogueString_MethodListTableEntryList* RogueString_MethodListTableEntryList__add( RogueString_MethodListTableEntryList* THIS, RogueString_MethodListTableEntryList* other_0 );
RogueInteger RogueString_MethodListTableEntryList__capacity( RogueString_MethodListTableEntryList* THIS );
RogueString_MethodListTableEntryList* RogueString_MethodListTableEntryList__clear( RogueString_MethodListTableEntryList* THIS );
RogueString_MethodListTableEntryList* RogueString_MethodListTableEntryList__insert( RogueString_MethodListTableEntryList* THIS, RogueClassString_MethodListTableEntry* value_0, RogueInteger before_index_1 );
RogueClassString_MethodListTableEntry* RogueString_MethodListTableEntryList__last( RogueString_MethodListTableEntryList* THIS );
RogueClassString_MethodListTableEntry* RogueString_MethodListTableEntryList__random( RogueString_MethodListTableEntryList* THIS );
RogueString_MethodListTableEntryList* RogueString_MethodListTableEntryList__reserve( RogueString_MethodListTableEntryList* THIS, RogueInteger additional_count_0 );
RogueClassString_MethodListTableEntry* RogueString_MethodListTableEntryList__remove( RogueString_MethodListTableEntryList* THIS, RogueClassString_MethodListTableEntry* value_0 );
RogueClassString_MethodListTableEntry* RogueString_MethodListTableEntryList__remove_at( RogueString_MethodListTableEntryList* THIS, RogueInteger index_0 );
RogueClassString_MethodListTableEntry* RogueString_MethodListTableEntryList__remove_last( RogueString_MethodListTableEntryList* THIS );
RogueString* RogueString_MethodListTableEntry__type_name( RogueClassString_MethodListTableEntry* THIS );
RogueClassString_MethodListTableEntry* RogueString_MethodListTableEntry__init( RogueClassString_MethodListTableEntry* THIS, RogueString* _key_0, RogueMethodList* _value_1, RogueInteger _hash_2 );
RogueClassString_MethodListTableEntry* RogueString_MethodListTableEntry__init_object( RogueClassString_MethodListTableEntry* THIS );
RogueString* RogueString_MethodListTableEntryArray__type_name( RogueArray* THIS );
RogueString* RogueString_TypeTableEntryListOps__type_name( RogueClassString_TypeTableEntryListOps* THIS );
RogueClassString_TypeTableEntryListOps* RogueString_TypeTableEntryListOps__init_object( RogueClassString_TypeTableEntryListOps* THIS );
RogueString* RogueString_IntegerTableEntryListOps__type_name( RogueClassString_IntegerTableEntryListOps* THIS );
RogueClassString_IntegerTableEntryListOps* RogueString_IntegerTableEntryListOps__init_object( RogueClassString_IntegerTableEntryListOps* THIS );
RogueString* RogueCmdCallInlineNativeRoutine__type_name( RogueClassCmdCallInlineNativeRoutine* THIS );
RogueClassCmd* RogueCmdCallInlineNativeRoutine__clone( RogueClassCmdCallInlineNativeRoutine* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassType* Rogue_CmdCallInlineNativeRoutine__type( RogueClassCmdCallInlineNativeRoutine* THIS );
RogueClassCmdCallInlineNativeRoutine* RogueCmdCallInlineNativeRoutine__init_object( RogueClassCmdCallInlineNativeRoutine* THIS );
RogueClassCmdCallInlineNativeRoutine* RogueCmdCallInlineNativeRoutine__init( RogueClassCmdCallInlineNativeRoutine* THIS, RogueClassToken* _auto_813_0, RogueClassMethod* _auto_814_1, RogueClassCmdArgs* _auto_815_2 );
RogueString* RogueCmdCallInlineNative__to_String( RogueClassCmdCallInlineNative* THIS );
RogueString* RogueCmdCallInlineNative__type_name( RogueClassCmdCallInlineNative* THIS );
void RogueCmdCallInlineNative__write_cpp( RogueClassCmdCallInlineNative* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmdCallInlineNative* RogueCmdCallInlineNative__init_object( RogueClassCmdCallInlineNative* THIS );
void RogueCmdCallInlineNative__print_this( RogueClassCmdCallInlineNative* THIS, RogueClassCPPWriter* writer_0 );
RogueString* RogueCmdCallNativeRoutine__type_name( RogueClassCmdCallNativeRoutine* THIS );
RogueClassCmd* RogueCmdCallNativeRoutine__clone( RogueClassCmdCallNativeRoutine* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdCallNativeRoutine__write_cpp( RogueClassCmdCallNativeRoutine* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmdCallNativeRoutine* RogueCmdCallNativeRoutine__init_object( RogueClassCmdCallNativeRoutine* THIS );
RogueClassCmdCallNativeRoutine* RogueCmdCallNativeRoutine__init( RogueClassCmdCallNativeRoutine* THIS, RogueClassToken* _auto_816_0, RogueClassMethod* _auto_817_1, RogueClassCmdArgs* _auto_818_2 );
RogueString* RogueCmdReadArrayCount__type_name( RogueClassCmdReadArrayCount* THIS );
RogueClassCmd* RogueCmdReadArrayCount__clone( RogueClassCmdReadArrayCount* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdReadArrayCount__write_cpp( RogueClassCmdReadArrayCount* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdReadArrayCount__resolve( RogueClassCmdReadArrayCount* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdReadArrayCount__type( RogueClassCmdReadArrayCount* THIS );
RogueClassCmdReadArrayCount* RogueCmdReadArrayCount__init_object( RogueClassCmdReadArrayCount* THIS );
RogueClassCmdReadArrayCount* RogueCmdReadArrayCount__init( RogueClassCmdReadArrayCount* THIS, RogueClassToken* _auto_822_0, RogueClassCmd* _auto_823_1 );
RogueString* RogueCmdCallInlineNativeMethod__type_name( RogueClassCmdCallInlineNativeMethod* THIS );
RogueClassCmd* RogueCmdCallInlineNativeMethod__clone( RogueClassCmdCallInlineNativeMethod* THIS, RogueClassCloneArgs* clone_args_0 );
RogueClassType* Rogue_CmdCallInlineNativeMethod__type( RogueClassCmdCallInlineNativeMethod* THIS );
RogueClassCmdCallInlineNativeMethod* RogueCmdCallInlineNativeMethod__init_object( RogueClassCmdCallInlineNativeMethod* THIS );
void RogueCmdCallInlineNativeMethod__print_this( RogueClassCmdCallInlineNativeMethod* THIS, RogueClassCPPWriter* writer_0 );
RogueString* RogueCmdCallNativeMethod__type_name( RogueClassCmdCallNativeMethod* THIS );
RogueClassCmd* RogueCmdCallNativeMethod__clone( RogueClassCmdCallNativeMethod* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdCallNativeMethod__write_cpp( RogueClassCmdCallNativeMethod* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmdCallNativeMethod* RogueCmdCallNativeMethod__init_object( RogueClassCmdCallNativeMethod* THIS );
RogueString* RogueCmdCallAspectMethod__type_name( RogueClassCmdCallAspectMethod* THIS );
RogueClassCmd* RogueCmdCallAspectMethod__clone( RogueClassCmdCallAspectMethod* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdCallAspectMethod__write_cpp( RogueClassCmdCallAspectMethod* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmdCallAspectMethod* RogueCmdCallAspectMethod__init_object( RogueClassCmdCallAspectMethod* THIS );
RogueString* RogueCmdCallDynamicMethod__type_name( RogueClassCmdCallDynamicMethod* THIS );
RogueClassCmd* RogueCmdCallDynamicMethod__clone( RogueClassCmdCallDynamicMethod* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdCallDynamicMethod__write_cpp( RogueClassCmdCallDynamicMethod* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmdCallDynamicMethod* RogueCmdCallDynamicMethod__init_object( RogueClassCmdCallDynamicMethod* THIS );
RogueString* RogueCmdCallMethod__type_name( RogueClassCmdCallMethod* THIS );
RogueClassCmd* RogueCmdCallMethod__call_prior( RogueClassCmdCallMethod* THIS, RogueClassScope* scope_0 );
RogueClassCmdCallMethod* RogueCmdCallMethod__init_object( RogueClassCmdCallMethod* THIS );
RogueString* RogueCandidateMethods__type_name( RogueClassCandidateMethods* THIS );
RogueClassCandidateMethods* RogueCandidateMethods__init( RogueClassCandidateMethods* THIS, RogueClassType* _auto_826_0, RogueClassCmdAccess* _auto_827_1, RogueLogical _auto_828_2 );
RogueLogical RogueCandidateMethods__has_match( RogueClassCandidateMethods* THIS );
RogueClassMethod* RogueCandidateMethods__match( RogueClassCandidateMethods* THIS );
RogueLogical RogueCandidateMethods__refine_matches( RogueClassCandidateMethods* THIS );
RogueLogical RogueCandidateMethods__update_available( RogueClassCandidateMethods* THIS );
RogueLogical RogueCandidateMethods__update_matches( RogueClassCandidateMethods* THIS );
RogueLogical RogueCandidateMethods__update( RogueClassCandidateMethods* THIS, RogueLogical require_compatible_0 );
RogueClassCandidateMethods* RogueCandidateMethods__init_object( RogueClassCandidateMethods* THIS );
RogueString* RogueCmdControlStructureListOps__type_name( RogueClassCmdControlStructureListOps* THIS );
RogueClassCmdControlStructureListOps* RogueCmdControlStructureListOps__init_object( RogueClassCmdControlStructureListOps* THIS );
RogueString* RogueCmdTaskControlSectionArray__type_name( RogueArray* THIS );
RogueString* RogueString_MethodTableEntryListOps__type_name( RogueClassString_MethodTableEntryListOps* THIS );
RogueClassString_MethodTableEntryListOps* RogueString_MethodTableEntryListOps__init_object( RogueClassString_MethodTableEntryListOps* THIS );
RogueString* RogueString_TokenListTableEntryList__to_String( RogueString_TokenListTableEntryList* THIS );
RogueString* RogueString_TokenListTableEntryList__type_name( RogueString_TokenListTableEntryList* THIS );
RogueString_TokenListTableEntryList* RogueString_TokenListTableEntryList__init_object( RogueString_TokenListTableEntryList* THIS );
RogueString_TokenListTableEntryList* RogueString_TokenListTableEntryList__init( RogueString_TokenListTableEntryList* THIS );
RogueString_TokenListTableEntryList* RogueString_TokenListTableEntryList__init( RogueString_TokenListTableEntryList* THIS, RogueInteger initial_capacity_0 );
RogueString_TokenListTableEntryList* RogueString_TokenListTableEntryList__init( RogueString_TokenListTableEntryList* THIS, RogueInteger initial_capacity_0, RogueClassString_TokenListTableEntry* initial_value_1 );
RogueString_TokenListTableEntryList* RogueString_TokenListTableEntryList__clone( RogueString_TokenListTableEntryList* THIS );
RogueString_TokenListTableEntryList* RogueString_TokenListTableEntryList__add( RogueString_TokenListTableEntryList* THIS, RogueClassString_TokenListTableEntry* value_0 );
RogueString_TokenListTableEntryList* RogueString_TokenListTableEntryList__add( RogueString_TokenListTableEntryList* THIS, RogueString_TokenListTableEntryList* other_0 );
RogueInteger RogueString_TokenListTableEntryList__capacity( RogueString_TokenListTableEntryList* THIS );
RogueString_TokenListTableEntryList* RogueString_TokenListTableEntryList__clear( RogueString_TokenListTableEntryList* THIS );
RogueString_TokenListTableEntryList* RogueString_TokenListTableEntryList__insert( RogueString_TokenListTableEntryList* THIS, RogueClassString_TokenListTableEntry* value_0, RogueInteger before_index_1 );
RogueClassString_TokenListTableEntry* RogueString_TokenListTableEntryList__last( RogueString_TokenListTableEntryList* THIS );
RogueClassString_TokenListTableEntry* RogueString_TokenListTableEntryList__random( RogueString_TokenListTableEntryList* THIS );
RogueString_TokenListTableEntryList* RogueString_TokenListTableEntryList__reserve( RogueString_TokenListTableEntryList* THIS, RogueInteger additional_count_0 );
RogueClassString_TokenListTableEntry* RogueString_TokenListTableEntryList__remove( RogueString_TokenListTableEntryList* THIS, RogueClassString_TokenListTableEntry* value_0 );
RogueClassString_TokenListTableEntry* RogueString_TokenListTableEntryList__remove_at( RogueString_TokenListTableEntryList* THIS, RogueInteger index_0 );
RogueClassString_TokenListTableEntry* RogueString_TokenListTableEntryList__remove_last( RogueString_TokenListTableEntryList* THIS );
RogueString* RogueString_TokenListTableEntry__type_name( RogueClassString_TokenListTableEntry* THIS );
RogueClassString_TokenListTableEntry* RogueString_TokenListTableEntry__init( RogueClassString_TokenListTableEntry* THIS, RogueString* _key_0, RogueTokenList* _value_1, RogueInteger _hash_2 );
RogueClassString_TokenListTableEntry* RogueString_TokenListTableEntry__init_object( RogueClassString_TokenListTableEntry* THIS );
RogueString* RogueString_TokenListTableEntryArray__type_name( RogueArray* THIS );
RogueString* RogueString_TokenTypeTableEntryListOps__type_name( RogueClassString_TokenTypeTableEntryListOps* THIS );
RogueClassString_TokenTypeTableEntryListOps* RogueString_TokenTypeTableEntryListOps__init_object( RogueClassString_TokenTypeTableEntryListOps* THIS );
RogueString* RogueString_CmdTableEntryListOps__type_name( RogueClassString_CmdTableEntryListOps* THIS );
RogueClassString_CmdTableEntryListOps* RogueString_CmdTableEntryListOps__init_object( RogueClassString_CmdTableEntryListOps* THIS );
RogueString* RogueCmdTaskControlSectionListOps__type_name( RogueClassCmdTaskControlSectionListOps* THIS );
RogueClassCmdTaskControlSectionListOps* RogueCmdTaskControlSectionListOps__init_object( RogueClassCmdTaskControlSectionListOps* THIS );
RogueString* RogueCmdAdjustProperty__type_name( RogueClassCmdAdjustProperty* THIS );
RogueClassCmd* RogueCmdAdjustProperty__clone( RogueClassCmdAdjustProperty* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdAdjustProperty__write_cpp( RogueClassCmdAdjustProperty* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdAdjustProperty__resolve( RogueClassCmdAdjustProperty* THIS, RogueClassScope* scope_0 );
RogueClassType* Rogue_CmdAdjustProperty__type( RogueClassCmdAdjustProperty* THIS );
RogueClassCmdAdjustProperty* RogueCmdAdjustProperty__init_object( RogueClassCmdAdjustProperty* THIS );
RogueClassCmdAdjustProperty* RogueCmdAdjustProperty__init( RogueClassCmdAdjustProperty* THIS, RogueClassToken* _auto_907_0, RogueClassCmd* _auto_908_1, RogueClassProperty* _auto_909_2, RogueInteger _auto_910_3 );
RogueString* RogueString_TypeSpecializerTableEntryListOps__type_name( RogueClassString_TypeSpecializerTableEntryListOps* THIS );
RogueClassString_TypeSpecializerTableEntryListOps* RogueString_TypeSpecializerTableEntryListOps__init_object( RogueClassString_TypeSpecializerTableEntryListOps* THIS );
RogueString* RogueString_PropertyTableEntryListOps__type_name( RogueClassString_PropertyTableEntryListOps* THIS );
RogueClassString_PropertyTableEntryListOps* RogueString_PropertyTableEntryListOps__init_object( RogueClassString_PropertyTableEntryListOps* THIS );
RogueString* RogueString_MethodListTableEntryListOps__type_name( RogueClassString_MethodListTableEntryListOps* THIS );
RogueClassString_MethodListTableEntryListOps* RogueString_MethodListTableEntryListOps__init_object( RogueClassString_MethodListTableEntryListOps* THIS );
RogueString* RogueCmdCallStaticMethod__type_name( RogueClassCmdCallStaticMethod* THIS );
RogueClassCmd* RogueCmdCallStaticMethod__clone( RogueClassCmdCallStaticMethod* THIS, RogueClassCloneArgs* clone_args_0 );
void RogueCmdCallStaticMethod__write_cpp( RogueClassCmdCallStaticMethod* THIS, RogueClassCPPWriter* writer_0, RogueLogical is_statement_1 );
RogueClassCmd* RogueCmdCallStaticMethod__resolve( RogueClassCmdCallStaticMethod* THIS, RogueClassScope* scope_0 );
RogueClassCmdCallStaticMethod* RogueCmdCallStaticMethod__init_object( RogueClassCmdCallStaticMethod* THIS );
RogueString* RogueString_TokenListTableEntryListOps__type_name( RogueClassString_TokenListTableEntryListOps* THIS );
RogueClassString_TokenListTableEntryListOps* RogueString_TokenListTableEntryListOps__init_object( RogueClassString_TokenListTableEntryListOps* THIS );

extern RogueProgram Rogue_program;

