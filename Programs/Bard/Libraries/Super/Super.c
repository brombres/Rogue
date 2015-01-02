#include "Super.h"

#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <inttypes.h>

#if !defined(_WIN32)
#  include <sys/time.h>
#  include <unistd.h>
#  include <signal.h>
#  include <dirent.h>
#  include <sys/socket.h>
#  include <sys/uio.h>
#  include <sys/stat.h>
#  include <netdb.h>
#  include <errno.h>
#  include <pthread.h>
#endif

#if defined(ANDROID)
#  include <netinet/in.h>
#endif

#if defined(_WIN32)
#  include <direct.h>
#  define chdir _chdir
#endif

#if TARGET_OS_IPHONE 
#  include <sys/types.h>
#  include <sys/sysctl.h>
#endif


//=============================================================================
//  SuperIntList
//=============================================================================
SuperIntList* SuperIntList_create()
{
  return SuperIntList_init( SUPER_ALLOCATE( sizeof(SuperIntList) ) );
}

SuperIntList* SuperIntList_destroy( SuperIntList* list )
{
  if ( !list ) return NULL;
  SuperIntList_release( list );
  SUPER_FREE( list );
  return NULL;
}

SuperIntList* SuperIntList_init( SuperIntList* list )
{
  memset( list, 0, sizeof(SuperIntList) );
  return list;
}

void SuperIntList_release( SuperIntList* list )
{
  if ( !list ) return;
  if (list->data)
  {
    SUPER_FREE( list->data );
    list->data = NULL;
  }
  list->capacity = list->count = 0;
}

SuperIntList* SuperIntList_clear( SuperIntList* list )
{
  list->count = 0;
  return list;
}

void SuperIntList_ensure_capacity( SuperIntList* list, int min_capacity )
{
  if (list->capacity >= min_capacity) return;

  if (list->data)
  {
    int* new_data = SUPER_ALLOCATE( sizeof(int) * min_capacity );
    memcpy( new_data, list->data, list->count * sizeof(int) );
    SUPER_FREE( list->data );
    list->data = new_data;
  }
  else
  {
    list->data = SUPER_ALLOCATE( sizeof(int) * min_capacity );
  }
  list->capacity = min_capacity;
}

void SuperIntList_add( SuperIntList* list, int value )
{
  if (list->data)
  {
    if (list->count == list->capacity)
    {
      SuperIntList_ensure_capacity( list, list->capacity<<1 );
    }
  }
  else
  {
    SuperIntList_ensure_capacity( list, 10 );
  }

  list->data[ list->count++ ] = value;
}

int SuperIntList_locate( SuperIntList* list, int value )
{
  int  i;
  int  count = list->count;
  int* data = list->data;

  for (i=0; i<count; ++i)
  {
    if (data[i] == value) return i;
  }

  return -1;
}

int SuperIntList_remove_last( SuperIntList* list )
{
  return list->data[ --list->count ];
}


//=============================================================================
//  SuperPointerList
//=============================================================================
SuperPointerList* SuperPointerList_create()
{
  return SuperPointerList_init( SUPER_ALLOCATE( sizeof(SuperPointerList) ) );
}

SuperPointerList* SuperPointerList_destroy( SuperPointerList* list )
{
  if (list)
  {
    SuperPointerList_release( list );
    SUPER_FREE( list );
  }
  return NULL;
}

SuperPointerList* SuperPointerList_init( SuperPointerList* list )
{
  memset( list, 0, sizeof(SuperPointerList) );
  return list;
}

void SuperPointerList_release( SuperPointerList* list )
{
  if ( !list ) return;
  if (list->data)
  {
    SUPER_FREE( list->data );
    list->data = NULL;
  }
  list->capacity = list->count = 0;
}

SuperPointerList* SuperPointerList_clear( SuperPointerList* list )
{
  list->count = 0;
  return list;
}

SuperPointerList* SuperPointerList_free_elements( SuperPointerList* list )
{
  while (list->count > 0)
  {
    SUPER_FREE( list->data[ --list->count ] );
  }
  return list;
}

void SuperPointerList_ensure_capacity( SuperPointerList* list, int min_capacity )
{
  if (list->capacity >= min_capacity) return;

  if (list->data)
  {
    void** new_data = SUPER_ALLOCATE( sizeof(void*) * min_capacity );
    memcpy( new_data, list->data, list->count * sizeof(void*) );
    SUPER_FREE( list->data );
    list->data = new_data;
  }
  else
  {
    list->data = SUPER_ALLOCATE( sizeof(void*) * min_capacity );
  }
  list->capacity = min_capacity;
}

void SuperPointerList_add( SuperPointerList* list, void* value )
{
  if (list->data)
  {
    if (list->count == list->capacity)
    {
      SuperPointerList_ensure_capacity( list, list->capacity<<1 );
    }
  }
  else
  {
    SuperPointerList_ensure_capacity( list, 10 );
  }

  list->data[ list->count++ ] = value;
}

void SuperPointerList_set( SuperPointerList* list, int index, void* value )
{
  SuperPointerList_ensure_capacity( list, index+1 );
  if (list->count <= index) list->count = index + 1;

  list->data[ index ] = value;
}


//=============================================================================
//  SuperPointerTable
//  Looks up void* pointers values using void* keys.
//=============================================================================
SuperPointerTable* SuperPointerTable_create( int bin_count )
{
  int count;
  SuperPointerTable* table = (SuperPointerTable*) SUPER_ALLOCATE( sizeof(SuperPointerTable) );

  // Ensure bin_count is a power of 2
  count = 1;
  while (count < bin_count) count <<= 1;
  bin_count = count;

  table->bins = (SuperPointerTableEntry**) SUPER_ALLOCATE( bin_count * sizeof(SuperPointerTableEntry*) );
  memset( table->bins, 0, bin_count * sizeof(SuperPointerTableEntry*) );
  table->bin_mask = bin_count - 1;

  return table;
}

SuperPointerTable* SuperPointerTable_destroy( SuperPointerTable* table )
{
  if (table)
  {
    SuperPointerTable_clear( table );
    SUPER_FREE( table->bins );
    SUPER_FREE( table );
  }
  return NULL;
}

void SuperPointerTable_clear( SuperPointerTable* table )
{
  int i;
  for (i=0; i<=table->bin_mask; ++i)
  {
    SuperPointerTableEntry* cur = table->bins[i];

    while (cur)
    {
      SuperPointerTableEntry* next = cur->next_entry;
      SUPER_FREE( cur );
      cur = next;
    }

    table->bins[i] = NULL;
  }
}

void* SuperPointerTable_get( SuperPointerTable* table, void* key )
{
  int slot = ((int)((intptr_t)key)) & table->bin_mask;
  SuperPointerTableEntry* cur = table->bins[slot];

  while (cur)
  {
    if (cur->key == key)
    {
      // Found existing entry
      return cur->value;
    }
    cur = cur->next_entry;
  }

  return NULL;
}

int SuperPointerTable_remove( SuperPointerTable* table, void* key )
{
  int slot = ((int)((intptr_t)key)) & table->bin_mask;
  SuperPointerTableEntry* cur = table->bins[slot];
  SuperPointerTableEntry* previous;

  if (cur == NULL) return 0;

  if (cur->key == key)
  {
    // Found item at head of linked list
    table->bins[slot] = cur->next_entry;
    SUPER_FREE( cur );
    return 1;
  }


  previous = cur;
  cur = cur->next_entry;
  while (cur)
  {
    if (cur->key == key)
    {
      previous->next_entry = cur->next_entry;
      SUPER_FREE( cur );
      return 1;
    }
    previous = cur;
    cur = cur->next_entry;
  }

  return 0;
}

void SuperPointerTable_set( SuperPointerTable* table, void* key, void* value )
{
  int slot = ((int)((intptr_t)key)) & table->bin_mask;
  SuperPointerTableEntry* first = table->bins[slot];
  SuperPointerTableEntry* cur = first;

  while (cur)
  {
    if (cur->key == key)
    {
      // Found existing entry
      cur->value = value;
      return;
    }
    cur = cur->next_entry;
  }

  // New entry
  cur = (SuperPointerTableEntry*) SUPER_ALLOCATE( sizeof(SuperPointerTableEntry) );
  cur->key = key;
  cur->value = value;
  cur->next_entry = first;
  table->bins[slot] = cur;
}


//=============================================================================
//  SuperResourceBank
//=============================================================================
SuperResourceBank* SuperResourceBank_create()
{
  SuperResourceBank* bank = (SuperResourceBank*) SUPER_ALLOCATE( sizeof(SuperResourceBank) );
  SuperResourceBank_init( bank );
  return bank;
}

SuperResourceBank* SuperResourceBank_destroy( SuperResourceBank* bank )
{
  if (bank) SUPER_FREE( bank );
  return NULL;
}

void SuperResourceBank_init( SuperResourceBank* bank )
{
  bank->resources = SuperPointerList_create();
  bank->info = SuperPointerList_create();
  bank->active_ids = SuperIntList_create();
  bank->available_ids = SuperIntList_create();

  SuperPointerList_add( bank->resources, NULL );  // skip spot 0
  SuperPointerList_add( bank->info, NULL );  // skip spot 0

  bank->id_lookup = SuperPointerTable_create( 64 );
}

void SuperResourceBank_release( SuperResourceBank* bank )
{
  bank->resources = SuperPointerList_destroy( bank->resources );
  bank->info      = SuperPointerList_destroy( bank->info );

  bank->active_ids = SuperIntList_destroy( bank->active_ids );
  bank->available_ids = SuperIntList_destroy( bank->available_ids );

  bank->id_lookup = SuperPointerTable_destroy( bank->id_lookup );
}

int SuperResourceBank_add( SuperResourceBank* bank, void* resource, void* info )
{
  int index = SuperResourceBank_create_id( bank );
  SuperResourceBank_set( bank, index, resource, info );
  SuperPointerTable_set( bank->id_lookup,  resource, (void*)(intptr_t)index );
  return index;
}

void SuperResourceBank_clear( SuperResourceBank* bank )
{
  SuperPointerList_clear( bank->resources );
  SuperPointerList_clear( bank->info );
  SuperIntList_clear( bank->active_ids );
  SuperIntList_clear( bank->available_ids );

  SuperPointerList_add( bank->resources, NULL );  // skip spot 0
  SuperPointerList_add( bank->info, NULL );       // skip spot 0

  SuperPointerTable_clear( bank->id_lookup );
}

int SuperResourceBank_count( SuperResourceBank* bank )
{
  return bank->resources->count;
}

int SuperResourceBank_create_id( SuperResourceBank* bank )
{
  int result;
  if (bank->available_ids->count)
  {
    result = SuperIntList_remove_last( bank->available_ids );
  }
  else
  {
    result = bank->resources->count;
    SuperPointerList_add( bank->resources, NULL );
    SuperPointerList_add( bank->info, NULL );
  }
  SuperIntList_add( bank->active_ids, result );
  return result;
}

int SuperResourceBank_find_id( SuperResourceBank* bank, void* resource )
{
  return (int)(intptr_t) SuperPointerTable_get( bank->id_lookup, resource );
}

int SuperResourceBank_find_first_active_id( SuperResourceBank* bank )
{
  if (bank->active_ids->count == 0) return 0;

  return bank->active_ids->data[0];
}

void* SuperResourceBank_get_resource( SuperResourceBank* bank, int id )
{
  if (id < 0 || id >= bank->resources->count) return NULL;
  return bank->resources->data[ id ];
}

void* SuperResourceBank_get_info( SuperResourceBank* bank, int id )
{
  if (id < 0 || id >= bank->resources->count) return NULL;
  return bank->info->data[ id ];
}

void* SuperResourceBank_remove( SuperResourceBank* bank, int id )
{
  int   index;
  void* result;

  if (id < 0 || id >= bank->resources->count) return NULL;

  SuperIntList_add( bank->available_ids, id );

  result = bank->resources->data[ id ];
  bank->resources->data[ id ] = NULL;
  bank->info->data[ id ] = NULL;

  index = SuperIntList_locate( bank->active_ids, id );
  if (index >= 0)
  {
    bank->active_ids->data[index] = bank->active_ids->data[ --bank->active_ids->count ];
  }

  return result;
}

void* SuperResourceBank_remove_first( SuperResourceBank* bank )
{
  int id = SuperResourceBank_find_first_active_id( bank );
  if ( !id ) return NULL;

  return SuperResourceBank_remove( bank, id );
}

void SuperResourceBank_set( SuperResourceBank* bank, int id, void* resource, void* info )
{
  if (id < 0 || id >= bank->resources->count) return;
  bank->resources->data[ id ] = resource;
  bank->info->data[ id ] = info;
}


//=============================================================================
//  Utility 
//=============================================================================
void* Super_allocate_bytes_and_clear( int size )
{
  void* result = SUPER_ALLOCATE( size );
  memset( result, 0, size );
  return result;
}

void* Super_free( void* ptr )
{
  if (ptr) SUPER_FREE( ptr );
  return NULL;
}

double  Super_current_time_in_seconds()
{
#if defined(_WIN32)
  struct __timeb64 time_struct;
  double time_seconds;
  _ftime64_s( &time_struct );
  time_seconds = (double) time_struct.time;
  time_seconds += time_struct.millitm / 1000.0;
  return time_seconds;
#elif defined(RVL)
  double time_seconds = OSTicksToMilliseconds(OSGetTime()) / 1000.0;
  return time_seconds;

#else
  struct timeval time_struct;
  double time_seconds;
  gettimeofday( &time_struct, 0 );
  time_seconds = (double) time_struct.tv_sec;
  time_seconds += (time_struct.tv_usec / 1000000.0);
  return time_seconds;
#endif
}

