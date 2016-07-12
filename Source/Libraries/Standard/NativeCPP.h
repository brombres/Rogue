//=============================================================================
//  NativeCPP.h
//=============================================================================

#if defined(ROGUE_DEBUG_BUILD)
  #define ROGUE_DEBUG_STATEMENT(_s_) _s_
#else
  #define ROGUE_DEBUG_STATEMENT(_s_)
#endif

#if defined(ROGUE_GCDEBUG_BUILD)
  #define ROGUE_GCDEBUG_STATEMENT(_s_) _s_
#else
  #define ROGUE_GCDEBUG_STATEMENT(_s_) ;
#endif

#if defined(_WIN32)
#  define ROGUE_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
#  define ROGUE_PLATFORM_MAC 1
#  define ROGUE_PLATFORM_UNIX_COMPATIBLE 1
#elif defined(__ANDROID__)
#  define ROGUE_PLATFORM_ANDROID 1
#elif defined(__linux__)
#  define ROGUE_PLATFORM_UNIX_COMPATIBLE 1
#else
#  define ROGUE_PLATFORM_GENERIC 1
#endif

#if defined(ROGUE_PLATFORM_WINDOWS)
#  include <windows.h>
#else
#  include <cstdint>
#endif

#include <stdlib.h>
#include <string.h>


//-----------------------------------------------------------------------------
//  Garbage Collection
//-----------------------------------------------------------------------------
#define ROGUE_DEF_LOCAL_REF(_t_,_n_, _v_) _t_ _n_ = _v_
#define ROGUE_DEF_LOCAL_REF_NULL(_t_,_n_) _t_ _n_ = 0
#define ROGUE_CREATE_REF(_t_,_n_) ((_t_)_n_)
#define ROGUE_ARG(_a_) _a_
#define ROGUE_DEF_COMPOUND_REF_PROP(_t_,_n_) RoguePtr<_t_> _n_

#define ROGUE_XINCREF(_o_)  (++((_o_)->reference_count))
#define ROGUE_XDECREF(_o_)  (--((_o_)->reference_count))
#define ROGUE_INCREF(_o_) if (_o_) (++((_o_)->reference_count))
#define ROGUE_DECREF(_o_) if (_o_) (--((_o_)->reference_count))

#define ROGUE_NEW_BYTES(_count_) malloc(_count_)
#define ROGUE_DEL_BYTES(_ptr_) free(_ptr_)

#define ROGUE_STL_ALLOCATOR std::allocator

extern void Rogue_configure_gc();

#ifdef ROGUE_GC_UNSAFE_COMPOUNDS
  #undef ROGUE_DEF_COMPOUND_REF_PROP
  #define ROGUE_DEF_COMPOUND_REF_PROP(_t_,_n_) _t_ _n_
#endif

#if ROGUE_GC_MODE_BOEHM
  #define GC_NAME_CONFLICT
  #include "gc.h"
  #include "gc_cpp.h"
  #include "gc_allocator.h"

  #undef ROGUE_STL_ALLOCATOR
  #define ROGUE_STL_ALLOCATOR traceable_allocator

  struct RogueObject;
  extern void Rogue_Boehm_IncRef (RogueObject*);
  extern void Rogue_Boehm_DecRef (RogueObject*);

  #undef ROGUE_NEW_BYTES
  #undef ROGUE_DEL_BYTES
  #define ROGUE_NEW_BYTES(_count_) ((void*)GC_MALLOC(_count_))
  //#define ROGUE_DEL_BYTES(_ptr_) GC_FREE(_ptr_)
  #define ROGUE_DEL_BYTES(_ptr_) /* May perform better! */

  #undef ROGUE_INCREF
  #undef ROGUE_DECREF
  #undef ROGUE_XINCREF
  #undef ROGUE_XDECREF
  #define ROGUE_INCREF(_o_) if (_o_) Rogue_Boehm_IncRef(_o_)
  #define ROGUE_DECREF(_o_) if (_o_) Rogue_Boehm_DecRef(_o_)
  #define ROGUE_XINCREF(_o_) Rogue_Boehm_IncRef(_o_)
  #define ROGUE_XDECREF(_o_) Rogue_Boehm_DecRef(_o_)
#endif

#if ROGUE_GC_MODE_AUTO
  #undef ROGUE_DEF_LOCAL_REF_NULL
  #define ROGUE_DEF_LOCAL_REF_NULL(_t_,_n_) RoguePtr<_t_> _n_;
  #undef ROGUE_DEF_LOCAL_REF
  #define ROGUE_DEF_LOCAL_REF(_t_,_n_, _v_) RoguePtr<_t_> _n_(_v_);
  #undef ROGUE_ARG
  #define ROGUE_ARG(_a_) rogue_ptr(_a_)
#endif

#define ROGUE_ATTRIBUTE_IS_CLASS            0
#define ROGUE_ATTRIBUTE_IS_ASPECT           1
#define ROGUE_ATTRIBUTE_IS_PRIMITIVE        2
#define ROGUE_ATTRIBUTE_IS_COMPOUND         3
#define ROGUE_ATTRIBUTE_TYPE_MASK           7

// AKA by-value type; not a reference type
#define ROGUE_ATTRIBUTE_IS_DIRECT           2

#define ROGUE_ATTRIBUTE_IS_NATIVE           32
#define ROGUE_ATTRIBUTE_IS_MACRO            64
#define ROGUE_ATTRIBUTE_IS_INITIALIZER      128
#define ROGUE_ATTRIBUTE_IS_IMMUTABLE        256
#define ROGUE_ATTRIBUTE_IS_GLOBAL           512
#define ROGUE_ATTRIBUTE_IS_SINGLETON        1024
#define ROGUE_ATTRIBUTE_IS_INCORPORATED     2048
#define ROGUE_ATTRIBUTE_IS_GENERATED        4096
#define ROGUE_ATTRIBUTE_IS_REQUISITE        8192
#define ROGUE_ATTRIBUTE_IS_TASK             16384
#define ROGUE_ATTRIBUTE_IS_TASK_CONVERSION  32768
#define ROGUE_ATTRIBUTE_IS_AUGMENT          65536
#define ROGUE_ATTRIBUTE_IS_ABSTRACT         131072
#define ROGUE_ATTRIBUTE_IS_ROUTINE          262144
#define ROGUE_ATTRIBUTE_IS_FALLBACK         524288
#define ROGUE_ATTRIBUTE_IS_SPECIAL          1048576
#define ROGUE_ATTRIBUTE_IS_PROPAGATED       2097152

template <class T>
struct RoguePtr
{
  T o;
  RoguePtr ( ) : o(0) { }

  RoguePtr (  T oo )
   : o(oo)
  {
    ROGUE_GCDEBUG_STATEMENT(printf("ref "));
    ROGUE_GCDEBUG_STATEMENT(show());
    ROGUE_INCREF(o);
  }

  RoguePtr (const RoguePtr<T> & oo)
   : o(oo.o)
  {
    ROGUE_GCDEBUG_STATEMENT(printf("ref "));
    ROGUE_GCDEBUG_STATEMENT(show());
    ROGUE_INCREF(o);
  }

  template <class O>
  operator O ()
  {
    return (O)o;
  }

  operator T ()
  {
    return o;
  }

  RoguePtr & operator= ( T oo )
  {
    release();
    o = oo;
    ROGUE_INCREF(o);
    ROGUE_GCDEBUG_STATEMENT(printf("assign "));
    ROGUE_GCDEBUG_STATEMENT(show());
    return *this;
  }

  bool operator==( RoguePtr<T> other ) const
  {
    return (o == other.o);
  }

  bool operator!=( RoguePtr<T> other ) const
  {
    return (o != other.o);
  }

  T& operator->()
  {
    return o;
  }

  void release ()
  {
    if (!o) return;
    ROGUE_DECREF(o);
    ROGUE_GCDEBUG_STATEMENT( if (o->reference_count == 0) show() );
    if (o->reference_count < 0) o->reference_count = 0;
    o = 0;
  }

  ~RoguePtr ()
  {
    release();
  }

  void show () {
    printf("ptr:%p o:%p rc:%i\n", this, o, o ? o->reference_count : -42);
  }
};


template < class T, class U >
bool operator!=( const RoguePtr<T>& lhs, const RoguePtr<U>& rhs )
{
  return lhs.o != rhs.o;
}


template <class T>
RoguePtr<T> & rogue_ptr ( RoguePtr<T> & o )
{
  return o;
}

template <class T>
RoguePtr<T*> rogue_ptr ( T * p )
{
  return RoguePtr<T*>(p);
}

template <class T>
T rogue_ptr (T p)
{
  return p;
}


//-----------------------------------------------------------------------------
//  Basics (Primitive types, macros, etc.)
//-----------------------------------------------------------------------------
#if defined(ROGUE_PLATFORM_WINDOWS)
  typedef double           RogueReal64;
  typedef float            RogueReal32;
  typedef __int64          RogueInt64;
  typedef __int32          RogueInt32;
  typedef __int32          RogueCharacter;
  typedef unsigned __int16 RogueWord;
  typedef unsigned char    RogueByte;
  typedef bool             RogueLogical;
#else
  typedef double           RogueReal64;
  typedef float            RogueReal32;
  typedef int64_t          RogueInt64;
  typedef int32_t          RogueInt32;
  typedef int32_t          RogueCharacter;
  typedef uint16_t         RogueWord;
  typedef uint8_t          RogueByte;
  typedef bool             RogueLogical;
#endif

struct RogueAllocator;
struct RogueArray;
struct RogueCharacterList;
struct RogueString;

#define ROGUE_CREATE_OBJECT(name) RogueType_create_object(RogueType##name,0)
  //e.g. RogueType_create_object(RogueStringBuilder,0)

#define ROGUE_SINGLETON(name) RogueType_singleton(RogueType##name)
  //e.g. RogueType_singleton( RogueTypeConsole )

#define ROGUE_PROPERTY(name) p_##name


//-----------------------------------------------------------------------------
//  Forward References
//-----------------------------------------------------------------------------
struct RogueObject;


//-----------------------------------------------------------------------------
//  Callback Definitions
//-----------------------------------------------------------------------------
typedef void         (*RogueCallback)();
typedef void         (*RogueTraceFn)( void* obj );
typedef RogueObject* (*RogueInitFn)( void* obj );
typedef void         (*RogueCleanUpFn)( void* obj );
typedef RogueString* (*RogueToStringFn)( void* obj );


//-----------------------------------------------------------------------------
//  RogueCallbackInfo
//-----------------------------------------------------------------------------
struct RogueCallbackInfo
{
  RogueCallback      callback;
  RogueCallbackInfo* next_callback_info;

  RogueCallbackInfo() : callback(0), next_callback_info(0) {}
  RogueCallbackInfo( RogueCallback callback ) : callback(callback), next_callback_info(0) {}

  ~RogueCallbackInfo() { if (next_callback_info) delete next_callback_info; }

  void add( RogueCallback callback )
  {
    if (this->callback)
    {
      if (next_callback_info) next_callback_info->add( callback );
      else                    next_callback_info = new RogueCallbackInfo( callback );
    }
    else
    {
      this->callback = callback;
    }
  }

  void call()
  {
    if (callback) callback();
    if (next_callback_info) next_callback_info->call();
  }
};


//-----------------------------------------------------------------------------
//  RogueType
//-----------------------------------------------------------------------------
struct RogueTypeInfo;

struct RogueType
{
  RogueTypeInfo*  type_info;

  int          name_index;

  int          base_type_count;
  RogueType**  base_types;

  int          index;  // used for aspect call dispatch
  int          object_size;
  int          attributes;

  int          global_property_count;
  int*         global_property_name_indices;
  int*         global_property_type_indices;
  void**       global_property_pointers;

  int          property_count;
  int*         property_name_indices;
  int*         property_type_indices;
  int*         property_offsets;

  RogueObject* _singleton;
  void**       methods;

  RogueAllocator*   allocator;

  RogueTraceFn      trace_fn;
  RogueInitFn       init_object_fn;
  RogueInitFn       init_fn;
  RogueCleanUpFn    clean_up_fn;
  RogueToStringFn   to_string_fn;
};

RogueArray*  RogueType_create_array( int count, int element_size, bool is_reference_array=false );
RogueObject* RogueType_create_object( RogueType* THIS, RogueInt32 size );
RogueTypeInfo*  RogueType_type_info( RogueType* THIS );
RogueString* RogueType_name( RogueType* THIS );
bool         RogueType_name_equals( RogueType* THIS, const char* name );
void         RogueType_print_name( RogueType* THIS );
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
#if defined(ROGUE_CUSTOM_OBJECT_PROPERTY)
ROGUE_CUSTOM_OBJECT_PROPERTY
#endif

  RogueObject* next_object;
  // Used to keep track of this allocation so that it can be freed when no
  // longer referenced.

  RogueType*   type;
  // Type info for this object.

  RogueInt32 object_size;
  // Set to be ~object_size when traced through during a garbage collection,
  // then flipped back again at the end of GC.

  RogueInt32 reference_count;
  // A positive reference_count ensures that this object will never be
  // collected.  A zero reference_count means this object is kept only as
  // long as it is visible to the memory manager.
};

RogueObject* RogueObject_as( RogueObject* THIS, RogueType* specialized_type );
RogueLogical RogueObject_instance_of( RogueObject* THIS, RogueType* ancestor_type );
void*        RogueObject_retain( RogueObject* THIS );
void*        RogueObject_release( RogueObject* THIS );
RogueString* RogueObject_to_string( RogueObject* THIS );

void RogueObject_trace( void* obj );
void RogueString_trace( void* obj );
void RogueArray_trace( void* obj );


//-----------------------------------------------------------------------------
//  RogueString
//-----------------------------------------------------------------------------
struct RogueString : RogueObject
{
  RogueInt32 byte_count;       // in UTF-8 bytes
  RogueInt32 character_count;  // in whole characters
  RogueInt32 is_ascii;
  RogueInt32 cursor_offset;
  RogueInt32 cursor_index;
  RogueInt32 hash_code;
  RogueByte  utf8[];
};

RogueString* RogueString_create_with_byte_count( int byte_count );
RogueString* RogueString_create_from_utf8( const char* utf8, int count=-1 );
RogueString* RogueString_create_from_characters( RogueCharacterList* characters );
void         RogueString_print_string( RogueString* st );
void         RogueString_print_characters( RogueCharacter* characters, int count );
void         RogueString_print_utf8( RogueByte* utf8, int count );

RogueCharacter RogueString_character_at( RogueString* THIS, int index );
RogueInt32     RogueString_set_cursor( RogueString* THIS, int index );
RogueString*   RogueString_validate( RogueString* THIS );


//-----------------------------------------------------------------------------
//  RogueArray
//-----------------------------------------------------------------------------
struct RogueArray : RogueObject
{
  int  count;
  int  element_size;
  bool is_reference_array;

  union
  {
    RogueObject*   as_objects[];
    RogueByte      as_logicals[];
    RogueByte      as_bytes[];
    RogueCharacter as_characters[];
    RogueInt32     as_int32s[];
    RogueInt64     as_int64s[];
    RogueReal32    as_real32s[];
    RogueReal64    as_real64s[];
  };
};

RogueArray* RogueArray_set( RogueArray* THIS, RogueInt32 i1, RogueArray* other, RogueInt32 other_i1, RogueInt32 copy_count );


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
//  RogueAllocationPage
//-----------------------------------------------------------------------------
struct RogueAllocationPage
{
  // Backs small 0..256-byte allocations.
  RogueAllocationPage* next_page;

  RogueByte* cursor;
  int        remaining;

  RogueByte  data[ ROGUEMM_PAGE_SIZE ];
};

RogueAllocationPage* RogueAllocationPage_create( RogueAllocationPage* next_page );
RogueAllocationPage* RogueAllocationPage_delete( RogueAllocationPage* THIS );
void*                RogueAllocationPage_allocate( RogueAllocationPage* THIS, int size );


//-----------------------------------------------------------------------------
//  RogueAllocator
//-----------------------------------------------------------------------------
struct RogueAllocator
{
  RogueAllocationPage* pages;
  RogueObject*         objects;
  RogueObject*         objects_requiring_cleanup;
  RogueObject*         available_objects[ROGUEMM_SLOT_COUNT];
};

RogueAllocator* RogueAllocator_create();
RogueAllocator* RogueAllocator_delete( RogueAllocator* THIS );

void*        RogueAllocator_allocate( int size );
RogueObject* RogueAllocator_allocate_object( RogueAllocator* THIS, RogueType* of_type, int size );
void*        RogueAllocator_free( RogueAllocator* THIS, void* data, int size );
void         RogueAllocator_free_objects( RogueAllocator* THIS );
void         RogueAllocator_collect_garbage( RogueAllocator* THIS );

extern int                Rogue_allocator_count;
extern RogueAllocator     Rogue_allocators[];
extern int                Rogue_type_count;
extern RogueType          Rogue_types[];
extern int                Rogue_type_info_table[];
extern int                Rogue_type_name_index_table[];
extern int                Rogue_object_size_table[];
extern void*              Rogue_global_property_pointers[];
extern int                Rogue_property_offsets[];
extern int                Rogue_attributes_table[];
extern void*              Rogue_dynamic_method_table[];
//extern int                Rogue_property_info_table[][];
extern RogueInitFn        Rogue_init_object_fn_table[];
extern RogueInitFn        Rogue_init_fn_table[];
extern RogueTraceFn       Rogue_trace_fn_table[];
extern RogueCleanUpFn     Rogue_clean_up_fn_table[];
extern RogueToStringFn    Rogue_to_string_fn_table[];
extern int                Rogue_literal_string_count;
extern RogueString*       Rogue_literal_strings[];
extern RogueObject*       Rogue_error_object;
extern RogueLogical       Rogue_configured;
extern int                Rogue_argc;
extern const char**       Rogue_argv;
extern int                Rogue_allocation_bytes_until_gc;
extern int                Rogue_gc_threshold;
extern bool               Rogue_gc_requested;
extern RogueCallbackInfo  Rogue_on_gc_begin;
extern RogueCallbackInfo  Rogue_on_gc_trace_finished;
extern RogueCallbackInfo  Rogue_on_gc_end;

struct RogueWeakReference;
extern RogueWeakReference* Rogue_weak_references;

void Rogue_configure( int argc=0, const char* argv[]=0 );
bool Rogue_collect_garbage( bool forced=false );
void Rogue_launch();
void Rogue_quit();
bool Rogue_update_tasks();  // returns true if tasks are still active


//-----------------------------------------------------------------------------
//  RogueDebugTrace
//-----------------------------------------------------------------------------
struct RogueDebugTrace
{
  static char buffer[120];

  const char* method_signature;
  const char* filename;
  int line;
  RogueDebugTrace* previous_trace;

  RogueDebugTrace( const char* method_signature, const char* filename, int line );
  ~RogueDebugTrace();

  int   count();

  char* to_c_string();
};

void Rogue_print_stack_trace ( bool leading_newline=false);


//-----------------------------------------------------------------------------
//  Error Handling
//-----------------------------------------------------------------------------
#define ROGUE_TRY \
  try \
  {

#define ROGUE_END_TRY \
  }

#define ROGUE_THROW(_ErrorType,_error_object) \
  throw (_ErrorType*)_error_object

#define ROGUE_CATCH(_ErrorType,local_error_object) \
  } \
  catch (_ErrorType* local_error_object) \
  { \
    RogueCPPException _internal_exception_reference( local_error_object );

#define ROGUE_CATCH_NO_VAR(_ErrorType) \
  } \
  catch (_ErrorType* caught_error) \
  {

struct RogueCPPException
{
  RogueObject * err;
  RogueCPPException( RogueObject * err )
  : err(err)
  {
    Rogue_error_object = err;
    RogueObject_retain( err );
  }
  ~RogueCPPException()
  {
    RogueObject_release( err );
  }
};

extern void Rogue_terminate_handler ();


//=============================================================================
