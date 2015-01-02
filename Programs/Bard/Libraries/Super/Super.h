#ifndef SUPER_H
#define SUPER_H

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#ifdef __cplusplus
extern "C" {
#  define SUPER_BEGIN_HEADER extern "C" {
#  define SUPER_END_HEADER   }
#else
#  define SUPER_BEGIN_HEADER 
#  define SUPER_END_HEADER
#endif

#ifndef SUPER_ALLOCATE
#  define SUPER_ALLOCATE(bytes) malloc(bytes)
#endif

#ifndef SUPER_FREE
#  define SUPER_FREE(ptr) free(ptr)
#endif

#ifndef NULL
#  define NULL 0
#endif

#ifdef _WIN64
#  define SUPER_PLATFORM_WINDOWS
#  define SUPER_PLATFORM_WINDOWS_64
#elif _WIN32
#  define SUPER_PLATFORM_WINDOWS
#  define SUPER_PLATFORM_WINDOWS_32
#elif __APPLE__
#  include "TargetConditionals.h"
#  if TARGET_IPHONE_SIMULATOR
#    define SUPER_PLATFORM_IOS
#    define SUPER_PLATFORM_IOS_SIMULATOR
#    define SUPER_PLATFORM_MOBILE
#  elif TARGET_OS_IPHONE
#    define SUPER_PLATFORM_IOS
#    define SUPER_PLATFORM_IOS_DEVICE
#    define SUPER_PLATFORM_MOBILE
#  else
//   TARGET_OS_MAC
#    define SUPER_PLATFORM_MAC
#  endif
#else
// __linux or __unix
#  define SUPER_PLATFORM_UNIX
#endif

//=============================================================================
//  Type Defs
//=============================================================================
#if defined(_WIN32)
  typedef __int32          SuperInteger;
  typedef unsigned __int16 SuperCharacter;
  typedef unsigned char    SuperByte;
  typedef double           SuperReal;
  typedef SuperInteger     SuperLogical;
  typedef float            SuperReal32;
  typedef __int64          SuperInt64;
#else
  typedef int              SuperInteger;
  typedef unsigned short   SuperCharacter;
  typedef unsigned char    SuperByte;
  typedef double           SuperReal;
  typedef SuperInteger     SuperLogical;
  typedef float            SuperReal32;
  typedef long long        SuperInt64;
#endif

//=============================================================================
//  SuperIntList
//=============================================================================
typedef struct SuperIntList
{
  int*   data;
  int    capacity;
  int    count;
} SuperIntList;

SuperIntList* SuperIntList_create();
SuperIntList* SuperIntList_destroy( SuperIntList* list );

SuperIntList* SuperIntList_init( SuperIntList* list );
void           SuperIntList_release( SuperIntList* list );

SuperIntList* SuperIntList_clear( SuperIntList* list );

void SuperIntList_ensure_capacity( SuperIntList* list, int min_capacity );
void SuperIntList_add( SuperIntList* list, int value );
int  SuperIntList_locate( SuperIntList* list, int value );
int  SuperIntList_remove_last( SuperIntList* list );


//=============================================================================
//  SuperPointerList
//=============================================================================
typedef struct SuperPointerList
{
  void** data;
  int    capacity;
  int    count;
} SuperPointerList;

SuperPointerList* SuperPointerList_create();
SuperPointerList* SuperPointerList_destroy( SuperPointerList* list );

SuperPointerList* SuperPointerList_init( SuperPointerList* list );
void             SuperPointerList_release( SuperPointerList* list );

SuperPointerList* SuperPointerList_clear( SuperPointerList* list );
SuperPointerList* SuperPointerList_free_elements( SuperPointerList* list );

void  SuperPointerList_ensure_capacity( SuperPointerList* list, int min_capacity );
void  SuperPointerList_add( SuperPointerList* list, void* value );
void  SuperPointerList_set( SuperPointerList* list, int index, void* value );


//=============================================================================
//  SuperPointerTable
//  Looks up void* pointers values using void* keys.
//=============================================================================
typedef struct SuperPointerTableEntry
{
  struct SuperPointerTableEntry* next_entry;
  void*  key;
  void*  value;
} SuperPointerTableEntry;

typedef struct SuperPointerTable
{
  SuperPointerTableEntry** bins;
  int                      bin_mask;
} SuperPointerTable;

SuperPointerTable* SuperPointerTable_create( int bin_count );
SuperPointerTable* SuperPointerTable_destroy( SuperPointerTable* table );

void  SuperPointerTable_clear( SuperPointerTable* table );
void* SuperPointerTable_get( SuperPointerTable* table, void* key );
int   SuperPointerTable_remove( SuperPointerTable* table, void* key );
void  SuperPointerTable_set( SuperPointerTable* table, void* key, void* value );


//=============================================================================
//  SuperResourceBank 
//=============================================================================
typedef struct SuperResourceBank
{
  SuperPointerList*  resources;
  SuperPointerList*  info;
  SuperIntList*      active_ids;
  SuperIntList*      available_ids;
  SuperPointerTable* id_lookup;  // Maps void* resource keys to int indices
} SuperResourceBank;

SuperResourceBank* SuperResourceBank_create();
SuperResourceBank* SuperResourceBank_destroy( SuperResourceBank* bank );

void SuperResourceBank_init( SuperResourceBank* bank );
void SuperResourceBank_release( SuperResourceBank* bank );

int   SuperResourceBank_add( SuperResourceBank* bank, void* resource, void* info );
void  SuperResourceBank_clear( SuperResourceBank* bank );
int   SuperResourceBank_count( SuperResourceBank* bank );
int   SuperResourceBank_create_id( SuperResourceBank* bank );
int   SuperResourceBank_find_id( SuperResourceBank* bank, void* resource );
int   SuperResourceBank_find_first_active_id( SuperResourceBank* bank );
void* SuperResourceBank_get_resource( SuperResourceBank* bank, int id );
void* SuperResourceBank_get_info( SuperResourceBank* bank, int id );
void* SuperResourceBank_remove( SuperResourceBank* bank, int id );
void* SuperResourceBank_remove_first( SuperResourceBank* bank );
void  SuperResourceBank_set( SuperResourceBank* bank, int id, void* data, void* info );


//=============================================================================
//  Utility 
//=============================================================================

#define Super_allocate(type)             ((type*)Super_allocate_bytes_and_clear(sizeof(type)))
#define Super_allocate_array(type,count) ((type*)Super_allocate_bytes_and_clear(count*sizeof(type)))
void*   Super_allocate_bytes_and_clear( int size ); // Supports convenience macro Super_allocate(type)
void*   Super_free( void* ptr );

double  Super_current_time_in_seconds();

char* Super_locate_asset( const char* path, const char* filename );
// 'path' - optional value that is prepended to filename.  Can be NULL or empty string.


SUPER_END_HEADER

#include "SuperEventSystem.h"

#endif

