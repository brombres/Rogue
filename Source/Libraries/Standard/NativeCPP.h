//=============================================================================
//  NativeCPP.h
//
//  Rogue runtime routines.
//=============================================================================

#if defined(ROGUE_DEBUG_BUILD)
  #define ROGUE_DEBUG_STATEMENT(_s_) _s_
  #include <assert>
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
  #if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE
    #define ROGUE_PLATFORM_IOS 1
  #else
    #define ROGUE_PLATFORM_MACOS 1
    #define ROGUE_PLATFORM_UNIX_COMPATIBLE 1
  #endif
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

#ifndef ROGUE_EXPORT_C
#  define ROGUE_EXPORT_C extern "C"
#endif
#ifndef ROGUE_EXPORT
#  define ROGUE_EXPORT extern
#endif

//-----------------------------------------------------------------------------
//  Multithreading
//-----------------------------------------------------------------------------
// When exiting Rogue code for a nontrivial amount of time (e.g., making a
// blocking call or returning from a native event handler), put a
// ROGUE_EXIT in your code.  When re-entering (e.g., after the blocking call
// or on entering a native event handler which is going to call Rogue code),
// do ROGUE_ENTER.
// ROGUE_BLOCKING_ENTER/EXIT do the same things but with the meanings reversed
// in case this makes it easier to think about.  An even easier way to make
// a blocking call is to simply wrap it in ROGUE_BLOCKING_CALL(foo(...)).
// The FAST variants are faster, but you need to be careful that they are
// exactly balanced.
// Of special note is that in the event handler case, you should have
// ROGUE_EXITed before the first event handler.  If you're using the FAST
// variants and don't do this, things will likely go quite badly for you.

#if ROGUE_GC_MODE_AUTO_MT

#define ROGUE_ENTER Rogue_mtgc_enter()
#define ROGUE_EXIT  Rogue_mtgc_exit()

#define ROGUE_ENTER_FAST Rogue_mtgc_B2_etc()
#define ROGUE_EXIT_FAST  Rogue_mtgc_B1()

inline void Rogue_mtgc_B1 (void);
inline void Rogue_mtgc_B2_etc (void);
inline void Rogue_mtgc_enter (void);
inline void Rogue_mtgc_exit (void);

template<typename RT> RT Rogue_mtgc_reenter (RT expr);

#define ROGUE_BLOCKING_CALL(__x) (ROGUE_EXIT, Rogue_mtgc_reenter((__x)))
#define ROGUE_BLOCKING_VOID_CALL(__x) do {ROGUE_EXIT; __x; ROGUE_ENTER;}while(false)

#else

#define ROGUE_ENTER
#define ROGUE_EXIT
#define ROGUE_ENTER_FAST
#define ROGUE_EXIT_FAST

#define ROGUE_BLOCKING_CALL(__x) __x

#endif

#define ROGUE_BLOCKING_ENTER ROGUE_EXIT
#define ROGUE_BLOCKING_EXIT  ROGUE_ENTER

//-----------------------------------------------------------------------------
//  Garbage Collection
//-----------------------------------------------------------------------------
#define ROGUE_DEF_LOCAL_REF(_t_,_n_, _v_) _t_ _n_ = _v_
#define ROGUE_DEF_LOCAL_REF_NULL(_t_,_n_) _t_ _n_ = 0
#define ROGUE_RETAIN_CATCH_VAR(_t_,_n_,_v_)
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

#if ROGUE_GC_MODE_BOEHM_TYPED
  #undef ROGUE_GC_MODE_BOEHM
  #define ROGUE_GC_MODE_BOEHM 1
  #include "gc_typed.h"
  void Rogue_init_boehm_type_info();
  #define ROGUE_GC_ALLOC_TYPE_UNTYPED 0
  #define ROGUE_GC_ALLOC_TYPE_ATOMIC 1
  #define ROGUE_GC_ALLOC_TYPE_TYPED 2
#endif

#if ROGUE_GC_MODE_BOEHM
  #define GC_NAME_CONFLICT
  #if ROGUE_THREAD_MODE
    // Assume GC built for the right thread mode!
    #define GC_THREADS 1
  #endif
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

#if ROGUE_GC_MODE_AUTO_ANY
  #undef ROGUE_DEF_LOCAL_REF_NULL
  #define ROGUE_DEF_LOCAL_REF_NULL(_t_,_n_) RoguePtr<_t_> _n_;
  #undef ROGUE_DEF_LOCAL_REF
  #define ROGUE_DEF_LOCAL_REF(_t_,_n_, _v_) RoguePtr<_t_> _n_(_v_);
  #undef ROGUE_RETAIN_CATCH_VAR
  #define ROGUE_RETAIN_CATCH_VAR(_t_,_n_,_v_) RoguePtr<_t_> _n_(_v_);
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

#define ROGUE_ATTRIBUTE_IS_API               (1 << 4)
#define ROGUE_ATTRIBUTE_IS_NATIVE            (1 << 5)
#define ROGUE_ATTRIBUTE_IS_MACRO             (1 << 6)
#define ROGUE_ATTRIBUTE_IS_INITIALIZER       (1 << 7)
#define ROGUE_ATTRIBUTE_IS_IMMUTABLE         (1 << 8)
#define ROGUE_ATTRIBUTE_IS_GLOBAL            (1 << 9)
#define ROGUE_ATTRIBUTE_IS_SINGLETON         (1 << 10)
#define ROGUE_ATTRIBUTE_IS_INCORPORATED      (1 << 11)
#define ROGUE_ATTRIBUTE_IS_GENERATED         (1 << 12)
#define ROGUE_ATTRIBUTE_IS_ESSENTIAL         (1 << 13)
#define ROGUE_ATTRIBUTE_IS_TASK              (1 << 14)
#define ROGUE_ATTRIBUTE_IS_TASK_CONVERSION   (1 << 15)
#define ROGUE_ATTRIBUTE_IS_AUGMENT           (1 << 16)
#define ROGUE_ATTRIBUTE_IS_ABSTRACT          (1 << 17)
#define ROGUE_ATTRIBUTE_IS_MUTATING          (1 << 18)
#define ROGUE_ATTRIBUTE_IS_FALLBACK          (1 << 19)
#define ROGUE_ATTRIBUTE_IS_SPECIAL           (1 << 20)
#define ROGUE_ATTRIBUTE_IS_PROPAGATED        (1 << 21)
#define ROGUE_ATTRIBUTE_IS_DYNAMIC           (1 << 22)
#define ROGUE_ATTRIBUTE_RETURNS_THIS         (1 << 23)
#define ROGUE_ATTRIBUTE_IS_PREFERRED         (1 << 24)
#define ROGUE_ATTRIBUTE_IS_NONapi            (1 << 25)
#define ROGUE_ATTRIBUTE_IS_DEPRECATED        (1 << 26)
#define ROGUE_ATTRIBUTE_IS_ENUM              (1 << 27)
#define ROGUE_ATTRIBUTE_IS_THREAD_LOCAL      (1 << 28)
#define ROGUE_ATTRIBUTE_IS_SYNCHRONIZED      (1 << 29)
#define ROGUE_ATTRIBUTE_IS_SYNCHRONIZABLE    (1 << 30)

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
//  Threading
//-----------------------------------------------------------------------------

#if ROGUE_THREAD_MODE != ROGUE_THREAD_MODE_NONE

#if ROGUE_GC_MODE_BOEHM
  #define ROGUE_THREAD_LOCALS_INIT(__first, __last) GC_add_roots((void*)&(__first), (void*)((&(__last))+1));
  #define ROGUE_THREAD_LOCALS_DEINIT(__first, __last) GC_remove_roots((void*)&(__first), (void*)((&(__last))+1));
#else
  #define ROGUE_THREAD_LOCALS_INIT(__first, __last)
  #define ROGUE_THREAD_LOCALS_DEINIT(__first, __last)
#endif

#endif

#if ROGUE_THREAD_MODE == ROGUE_THREAD_MODE_PTHREADS

#include <pthread.h>
#include <atomic>

#define ROGUE_THREAD_LOCAL thread_local

static inline void _rogue_init_mutex (pthread_mutex_t * mutex)
{
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(mutex, &attr);
}

class RogueUnlocker
{
  pthread_mutex_t & mutex;
public:
  RogueUnlocker(pthread_mutex_t & mutex)
  : mutex(mutex)
  {
    pthread_mutex_lock(&mutex);
  }
  ~RogueUnlocker (void)
  {
    pthread_mutex_unlock(&mutex);
  }
};

#define ROGUE_SYNC_OBJECT_TYPE pthread_mutex_t
#define ROGUE_SYNC_OBJECT_INIT _rogue_init_mutex(&THIS->_object_mutex);
#define ROGUE_SYNC_OBJECT_CLEANUP pthread_mutex_destroy(&THIS->_object_mutex);
#define ROGUE_SYNC_OBJECT_ENTER RogueUnlocker _unlocker(THIS->_object_mutex);
#define ROGUE_SYNC_OBJECT_EXIT

#elif ROGUE_THREAD_MODE == ROGUE_THREAD_MODE_CPP

#include <thread>
#include <mutex>
#include <atomic>

#define ROGUE_THREAD_LOCAL thread_local

class RogueUnlocker
{
  std::recursive_mutex & mutex;
public:
  RogueUnlocker(std::recursive_mutex & mutex)
  : mutex(mutex)
  {
    mutex.lock();
  }
  ~RogueUnlocker (void)
  {
    mutex.unlock();
  }
};

#define ROGUE_SYNC_OBJECT_TYPE std::recursive_mutex
#define ROGUE_SYNC_OBJECT_INIT
#define ROGUE_SYNC_OBJECT_CLEANUP
#define ROGUE_SYNC_OBJECT_ENTER RogueUnlocker _unlocker(THIS->_object_mutex);
#define ROGUE_SYNC_OBJECT_EXIT

#else

#define ROGUE_SYNC_OBJECT_TYPE
#define ROGUE_SYNC_OBJECT_INIT
#define ROGUE_SYNC_OBJECT_CLEANUP
#define ROGUE_SYNC_OBJECT_ENTER
#define ROGUE_SYNC_OBJECT_EXIT
#define ROGUE_THREAD_LOCAL
#define ROGUE_THREAD_LOCALS_INIT(__first, __last)
#define ROGUE_THREAD_LOCALS_DEINIT(__first, __last)

#endif


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
struct RogueType
{
  RogueObject* type_info;

  int          name_index;

  int          base_type_count;
  RogueType**  base_types;

  int          index;  // used for aspect call dispatch
  int          object_size;
  int          attributes;

  int          global_property_count;
  const int*   global_property_name_indices;
  const int*   global_property_type_indices;
  const void** global_property_pointers;

  int          property_count;
  const int*   property_name_indices;
  const int*   property_type_indices;
  const int*   property_offsets;

#if (ROGUE_THREAD_MODE == ROGUE_THREAD_MODE_PTHREADS) || (ROGUE_THREAD_MODE == ROGUE_THREAD_MODE_CPP)
  std::atomic<RogueObject*> _singleton;
#else
  RogueObject* _singleton;
#endif
  const void** methods; // first function pointer in Rogue_dynamic_method_table
  int          method_count;

  RogueAllocator*   allocator;

  RogueTraceFn      trace_fn;
  RogueInitFn       init_object_fn;
  RogueInitFn       init_fn;
  RogueCleanUpFn    on_cleanup_fn;
  RogueToStringFn   to_string_fn;

#if ROGUE_GC_MODE_BOEHM_TYPED
  int          gc_alloc_type;
  GC_descr     gc_type_descr;
#endif
};

ROGUE_EXPORT_C RogueArray*  RogueType_create_array( int count, int element_size, bool is_reference_array=false, int element_type_index=-1 ) ;
ROGUE_EXPORT_C RogueObject* RogueType_create_object( RogueType* THIS, RogueInt32 size );
ROGUE_EXPORT_C RogueLogical RogueType_instance_of( RogueType* THIS, RogueType* ancestor_type );
ROGUE_EXPORT_C RogueString* RogueType_name( RogueType* THIS );
ROGUE_EXPORT_C bool         RogueType_name_equals( RogueType* THIS, const char* name );
ROGUE_EXPORT_C void         RogueType_print_name( RogueType* THIS );
ROGUE_EXPORT_C RogueType*   RogueType_retire( RogueType* THIS );
ROGUE_EXPORT_C RogueObject* RogueType_singleton( RogueType* THIS );


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

ROGUE_EXPORT_C RogueObject* RogueObject_as( RogueObject* THIS, RogueType* specialized_type );
ROGUE_EXPORT_C RogueLogical RogueObject_instance_of( RogueObject* THIS, RogueType* ancestor_type );
ROGUE_EXPORT_C void*        RogueObject_retain( RogueObject* THIS );
ROGUE_EXPORT_C void*        RogueObject_release( RogueObject* THIS );
ROGUE_EXPORT_C RogueString* RogueObject_to_string( RogueObject* THIS );

ROGUE_EXPORT_C void RogueObject_trace( void* obj );
ROGUE_EXPORT_C void RogueString_trace( void* obj );
ROGUE_EXPORT_C void RogueArray_trace( void* obj );


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
#if ROGUE_GC_MODE_BOEHM_TYPED
  char       *utf8;
#else
  char       utf8[];
#endif
};

ROGUE_EXPORT_C RogueString* RogueString_create_with_byte_count( int byte_count );
ROGUE_EXPORT_C RogueString* RogueString_create_from_utf8( const char* utf8, int count=-1 );
ROGUE_EXPORT_C RogueString* RogueString_create_from_characters( RogueCharacterList* characters );
void         RogueString_print_string( RogueString* st );
void         RogueString_print_characters( RogueCharacter* characters, int count );
void         RogueString_print_utf8( char* utf8, int count );

RogueCharacter RogueString_character_at( RogueString* THIS, int index );
RogueInt32     RogueString_set_cursor( RogueString* THIS, int index );
RogueString*   RogueString_validate( RogueString* THIS );


//-----------------------------------------------------------------------------
//  RogueArray
//-----------------------------------------------------------------------------
#if defined(__clang__)
#define ROGUE_EMPTY_ARRAY
#elif defined(__GNUC__) || defined(__GNUG__)
#define ROGUE_EMPTY_ARRAY 0
#endif
struct RogueArray : RogueObject
{
  int  count;
  int  element_size;
  bool is_reference_array;

#if ROGUE_GC_MODE_BOEHM_TYPED
  union
  {
    RogueObject**   as_objects;
    RogueByte*      as_logicals;
    RogueByte*      as_bytes;
    RogueCharacter* as_characters;
    RogueInt32*     as_int32s;
    RogueInt64*     as_int64s;
    RogueReal32*    as_real32s;
    RogueReal64*    as_real64s;
  };
#else
  union
  {
    RogueObject*   as_objects[ROGUE_EMPTY_ARRAY];
    RogueByte      as_logicals[ROGUE_EMPTY_ARRAY];
    RogueByte      as_bytes[ROGUE_EMPTY_ARRAY];
    RogueCharacter as_characters[ROGUE_EMPTY_ARRAY];
    RogueInt32     as_int32s[ROGUE_EMPTY_ARRAY];
    RogueInt64     as_int64s[ROGUE_EMPTY_ARRAY];
    RogueReal32    as_real32s[ROGUE_EMPTY_ARRAY];
    RogueReal64    as_real64s[ROGUE_EMPTY_ARRAY];
  };
#endif
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
// Set to -1 to disable the small object allocator.
#ifndef ROGUEMM_SMALL_ALLOCATION_SIZE_LIMIT
#  define ROGUEMM_SMALL_ALLOCATION_SIZE_LIMIT  ((ROGUEMM_SLOT_COUNT-1) << ROGUEMM_GRANULARITY_BITS)
#endif


//-----------------------------------------------------------------------------
//  RogueAllocationPage
//-----------------------------------------------------------------------------
struct RogueAllocationPage
{
  // Backs small 0..256-byte allocations.
  RogueByte  data[ ROGUEMM_PAGE_SIZE ];

  RogueAllocationPage* next_page;

  RogueByte* cursor;
  int        remaining;
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
RogueObject* RogueAllocator_allocate_object( RogueAllocator* THIS, RogueType* of_type, int size, int element_type_index=-1 );
void*        RogueAllocator_free( RogueAllocator* THIS, void* data, int size );
void         RogueAllocator_free_objects( RogueAllocator* THIS );
void         RogueAllocator_free_all();
void         RogueAllocator_collect_garbage( RogueAllocator* THIS );

extern int                Rogue_allocator_count;
extern RogueAllocator     Rogue_allocators[];
extern int                Rogue_type_count;
extern RogueType          Rogue_types[];
extern const int          Rogue_type_info_table[];
extern const int          Rogue_type_name_index_table[];
extern const int          Rogue_object_size_table[];
extern const void*        Rogue_global_property_pointers[];
extern const int          Rogue_property_offsets[];
extern const int          Rogue_attributes_table[];
extern const void*        Rogue_dynamic_method_table[];
//extern int                Rogue_property_info_table[][];
extern RogueInitFn        Rogue_init_object_fn_table[];
extern RogueInitFn        Rogue_init_fn_table[];
extern RogueTraceFn       Rogue_trace_fn_table[];
extern RogueCleanUpFn     Rogue_on_cleanup_fn_table[];
extern RogueToStringFn    Rogue_to_string_fn_table[];
extern int                Rogue_literal_string_count;
extern RogueString*       Rogue_literal_strings[];
extern RogueLogical       Rogue_configured;
extern int                Rogue_argc;
extern const char**       Rogue_argv;
extern bool               Rogue_gc_logging;
extern int                Rogue_gc_threshold;
extern bool               Rogue_gc_requested;
extern RogueCallbackInfo  Rogue_on_gc_begin;
extern RogueCallbackInfo  Rogue_on_gc_trace_finished;
extern RogueCallbackInfo  Rogue_on_gc_end;

struct RogueWeakReference;
extern RogueWeakReference* Rogue_weak_references;

ROGUE_EXPORT_C void Rogue_configure( int argc=0, const char* argv[]=0 );
ROGUE_EXPORT_C bool Rogue_collect_garbage( bool forced=false );
ROGUE_EXPORT_C void Rogue_launch();
ROGUE_EXPORT_C void Rogue_init_thread();
ROGUE_EXPORT_C void Rogue_deinit_thread();
ROGUE_EXPORT_C void Rogue_quit();
ROGUE_EXPORT_C bool Rogue_update_tasks();  // returns true if tasks are still active


//-----------------------------------------------------------------------------
//  RogueDebugTrace
//-----------------------------------------------------------------------------
struct RogueDebugTrace
{
  static char buffer[512];

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
    ROGUE_RETAIN_CATCH_VAR( _ErrorType*, _internal_exception_reference, local_error_object );

#define ROGUE_CATCH_NO_VAR(_ErrorType) \
  } \
  catch (_ErrorType* caught_error) \
  {

extern void Rogue_terminate_handler ();

//=============================================================================
