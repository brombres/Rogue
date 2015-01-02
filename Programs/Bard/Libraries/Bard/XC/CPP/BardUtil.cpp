#include "BardXC.h"

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
//  Miscellaneous
//=============================================================================
int BardVMCString_hash_code( const char* st )
{
  int i;
  int hash_code = 0;
  for (i=0; st[i]; ++i)
  {
    hash_code = hash_code * 7 + st[i];
  }
  return hash_code;
}

int BardVMCString_find( const char* st, char ch )
{
  int i = -1;
  while (st[++i])
  {
    if (st[i] == ch) return i;
  }
  return -1;
}

char* BardVMCString_duplicate( const char* st )
{
  char* buffer;
  int   count;

  count = (int) strlen( st );

  buffer = (char*) BARD_ALLOCATE( count + 1 );

  strcpy( buffer, st );

  return buffer;
}


//=============================================================================
//  Memory Allocation
//=============================================================================
void* Bard_allocate_bytes_and_clear( int size )
{
  void* result = BARD_ALLOCATE( size );
  memset( result, 0, size );
  return result;
}

void* Bard_free( void* ptr )
{
  if (ptr) BARD_FREE( ptr );
  return NULL;
}


//=============================================================================
//  BardHashTable
//=============================================================================
BardHashTable* BardHashTable_create( int bin_count )
{
  int count;
  BardHashTable* table = (BardHashTable*) BARD_ALLOCATE( sizeof(BardHashTable) );

  // Ensure bin_count is a power of 2
  count = 1;
  while (count < bin_count) count <<= 1;
  bin_count = count;

  table->bins = (BardHashTableEntry**) BARD_ALLOCATE( bin_count * sizeof(BardHashTableEntry*) );
  memset( table->bins, 0, bin_count * sizeof(BardHashTableEntry*) );
  table->bin_mask = bin_count - 1;

  return table;
}

BardHashTable* BardHashTable_destroy( BardHashTable* table )
{
  if (table)
  {
    int i;
    for (i=0; i<=table->bin_mask; ++i)
    {
      BardHashTableEntry* cur = table->bins[i];
      while (cur)
      {
        BardHashTableEntry* next = cur->next_entry;
        BARD_FREE( cur->key );
        BARD_FREE( cur );
        cur = next;
      }
    }
    BARD_FREE( table->bins );
    BARD_FREE( table );
  }
  return NULL;
}

void* BardHashTable_get( BardHashTable* table, const char* key )
{
  int hash_code = BardVMCString_hash_code( key );
  int slot = hash_code & table->bin_mask;
  BardHashTableEntry* cur = table->bins[slot];

  while (cur)
  {
    if (cur->hash_code == hash_code && 0 == strcmp(cur->key,key))
    {
      // Found existing entry
      return cur->value;
    }
    cur = cur->next_entry;
  }

  return NULL;
}

void BardHashTable_set( BardHashTable* table, const char* key, void* value )
{
  int hash_code = BardVMCString_hash_code( key );
  int slot = hash_code & table->bin_mask;
  BardHashTableEntry* first = table->bins[slot];
  BardHashTableEntry* cur = first;

  while (cur)
  {
    if (cur->hash_code == hash_code && 0 == strcmp(cur->key,key))
    {
      // Found existing entry
      cur->value = value;
      return;
    }
    cur = cur->next_entry;
  }

  // New entry
  cur = (BardHashTableEntry*) BARD_ALLOCATE( sizeof(BardHashTableEntry) );
  cur->key = BardVMCString_duplicate( key );
  cur->value = value;
  cur->hash_code = hash_code;
  cur->next_entry = first;
  table->bins[slot] = cur;
}


//=============================================================================
//  BardIntList
//=============================================================================
BardIntList* BardIntList_create()
{
  return BardIntList_init( (BardIntList*) BARD_ALLOCATE( sizeof(BardIntList) ) );
}

BardIntList* BardIntList_destroy( BardIntList* list )
{
  if ( !list ) return NULL;
  BardIntList_release( list );
  BARD_FREE( list );
  return NULL;
}

BardIntList* BardIntList_init( BardIntList* list )
{
  memset( list, 0, sizeof(BardIntList) );
  return list;
}

void BardIntList_release( BardIntList* list )
{
  if ( !list ) return;
  if (list->data)
  {
    BARD_FREE( list->data );
    list->data = NULL;
  }
  list->capacity = list->count = 0;
}

BardIntList* BardIntList_clear( BardIntList* list )
{
  list->count = 0;
  return list;
}

void BardIntList_ensure_capacity( BardIntList* list, int min_capacity )
{
  if (list->capacity >= min_capacity) return;

  if (list->data)
  {
    int* new_data = (int*) BARD_ALLOCATE( sizeof(int) * min_capacity );
    memcpy( new_data, list->data, list->count * sizeof(int) );
    BARD_FREE( list->data );
    list->data = new_data;
  }
  else
  {
    list->data = (int*) BARD_ALLOCATE( sizeof(int) * min_capacity );
  }
  list->capacity = min_capacity;
}

void BardIntList_add( BardIntList* list, int value )
{
  if (list->data)
  {
    if (list->count == list->capacity)
    {
      BardIntList_ensure_capacity( list, list->capacity<<1 );
    }
  }
  else
  {
    BardIntList_ensure_capacity( list, 10 );
  }

  list->data[ list->count++ ] = value;
}

int BardIntList_remove_last( BardIntList* list )
{
  return list->data[ --list->count ];
}


//=============================================================================
//  BardPointerList
//=============================================================================
BardPointerList* BardPointerList_create()
{
  return BardPointerList_init( (BardPointerList*) BARD_ALLOCATE( sizeof(BardPointerList) ) );
}

void BardPointerList_destroy( BardPointerList* list )
{
  if ( !list ) return;
  BardPointerList_release( list );
  BARD_FREE( list );
}

BardPointerList* BardPointerList_init( BardPointerList* list )
{
  memset( list, 0, sizeof(BardPointerList) );
  return list;
}

void BardPointerList_release( BardPointerList* list )
{
  if ( !list ) return;
  if (list->data)
  {
    BARD_FREE( list->data );
    list->data = NULL;
  }
  list->capacity = list->count = 0;
}

BardPointerList* BardPointerList_clear( BardPointerList* list )
{
  list->count = 0;
  return list;
}

BardPointerList* BardPointerList_free_elements( BardPointerList* list )
{
  while (list->count > 0)
  {
    BARD_FREE( list->data[ --list->count ] );
  }
  return list;
}

void BardPointerList_ensure_capacity( BardPointerList* list, int min_capacity )
{
  if (list->capacity >= min_capacity) return;

  if (list->data)
  {
    void** new_data = (void**) BARD_ALLOCATE( sizeof(void*) * min_capacity );
    memcpy( new_data, list->data, list->count * sizeof(void*) );
    BARD_FREE( list->data );
    list->data = new_data;
  }
  else
  {
    list->data = (void**) BARD_ALLOCATE( sizeof(void*) * min_capacity );
  }
  list->capacity = min_capacity;
}

void BardPointerList_add( BardPointerList* list, void* value )
{
  if (list->data)
  {
    if (list->count == list->capacity)
    {
      BardPointerList_ensure_capacity( list, list->capacity<<1 );
    }
  }
  else
  {
    BardPointerList_ensure_capacity( list, 10 );
  }

  list->data[ list->count++ ] = value;
}

//=============================================================================
//  BardResourceIDTable
//  Looks up resource IDs using void* keys.
//=============================================================================
BardResourceIDTable* BardResourceIDTable_create( int bin_count )
{
  int count;
  BardResourceIDTable* table = (BardResourceIDTable*) BARD_ALLOCATE( sizeof(BardResourceIDTable) );

  // Ensure bin_count is a power of 2
  count = 1;
  while (count < bin_count) count <<= 1;
  bin_count = count;

  table->bins = (BardResourceIDTableEntry**) BARD_ALLOCATE( bin_count * sizeof(BardResourceIDTableEntry*) );
  memset( table->bins, 0, bin_count * sizeof(BardResourceIDTableEntry*) );
  table->bin_mask = bin_count - 1;

  return table;
}

BardResourceIDTable* BardResourceIDTable_destroy( BardResourceIDTable* table )
{
  if (table)
  {
    int i;
    for (i=0; i<=table->bin_mask; ++i)
    {
      BardResourceIDTableEntry* cur = table->bins[i];
      while (cur)
      {
        BardResourceIDTableEntry* next = cur->next_entry;
        BARD_FREE( cur );
        cur = next;
      }
    }
    BARD_FREE( table->bins );
    BARD_FREE( table );
  }
  return NULL;
}

int BardResourceIDTable_get( BardResourceIDTable* table, void* key )
{
  int slot = ((int)((intptr_t)key)) & table->bin_mask;
  BardResourceIDTableEntry* cur = table->bins[slot];

  while (cur)
  {
    if (cur->key == key)
    {
      // Found existing entry
      return cur->value;
    }
    cur = cur->next_entry;
  }

  return 0;
}

void BardResourceIDTable_set( BardResourceIDTable* table, void* key, int value )
{
  int slot = ((int)((intptr_t)key)) & table->bin_mask;
  BardResourceIDTableEntry* first = table->bins[slot];
  BardResourceIDTableEntry* cur = first;

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
  cur = (BardResourceIDTableEntry*) BARD_ALLOCATE( sizeof(BardResourceIDTableEntry) );
  cur->key = key;
  cur->value = value;
  cur->next_entry = first;
  table->bins[slot] = cur;
}


//=============================================================================
//  BardResourceBank
//=============================================================================
BardResourceBank* BardResourceBank_create()
{
  BardResourceBank* bank = (BardResourceBank*) BARD_ALLOCATE( sizeof(BardResourceBank) );
  BardResourceBank_init( bank );
  return bank;
}

BardResourceBank* BardResourceBank_destroy( BardResourceBank* bank )
{
  if (bank) BARD_FREE( bank );
  return NULL;
}

void BardResourceBank_init( BardResourceBank* bank )
{
  BardPointerList_init( &bank->resources );
  BardIntList_init( &bank->available_indices );
  BardPointerList_add( &bank->resources, NULL );  // skip spot 0
  bank->initialized = 1;
}

void BardResourceBank_release( BardResourceBank* bank )
{
  BardPointerList_release( &bank->resources );
  BardIntList_release( &bank->available_indices );
}

int BardResourceBank_add( BardResourceBank* bank, void* data )
{
  if ( !bank->initialized ) BardResourceBank_init( bank );

  int index = BardResourceBank_create_id( bank );
  BardResourceBank_set( bank, index, data );
  return index;
}

void BardResourceBank_clear( BardResourceBank* bank )
{
  if ( !bank->initialized ) return;

  BardPointerList_clear( &bank->resources );
  BardIntList_clear( &bank->available_indices );

  BardPointerList_add( &bank->resources, NULL );  // skip spot 0
}

int BardResourceBank_create_id( BardResourceBank* bank )
{
  if (bank->available_indices.count)
  {
    return BardIntList_remove_last( &bank->available_indices );
  }
  else
  {
    int result = bank->resources.count;
    BardPointerList_add( &bank->resources, NULL );
    return result;
  }
}

int BardResourceBank_find( BardResourceBank* bank, void* data )
{
  int capacity = bank->resources.capacity;
  int i;
  for (i=1; i<capacity; ++i)
  {
    void* resource = bank->resources.data[ i ];
    if (resource == data) return i;
  }

  return 0;
}

int BardResourceBank_find_first( BardResourceBank* bank )
{
  int capacity = bank->resources.capacity;
  int i;
  for (i=1; i<capacity; ++i)
  {
    void* resource = bank->resources.data[ i ];
    if (resource) return i;
  }

  return 0;
}

void* BardResourceBank_get( BardResourceBank* bank, int id )
{
  if (id < 0 || id >= bank->resources.count) return NULL;
  return bank->resources.data[ id ];
}

void* BardResourceBank_remove( BardResourceBank* bank, int id )
{
  void* result;

  if (id < 0 || id >= bank->resources.count) return NULL;

  result = bank->resources.data[ id ];
  bank->resources.data[ id ] = NULL;
  BardIntList_add( &bank->available_indices, id );
  return result;
}

void BardResourceBank_set( BardResourceBank* bank, int id, void* data )
{
  if (id < 0 || id >= bank->resources.count) return;
  bank->resources.data[ id ] = data;
}



//=============================================================================
//  BardUtil
//=============================================================================
BardXCReal BardUtil_time()
{
#if defined(_WIN32)
  struct __timeb64 time_struct;
  BardXCReal time_seconds;
  _ftime64_s( &time_struct );
  time_seconds = (BardXCReal) time_struct.time;
  time_seconds += time_struct.millitm / 1000.0;
  return time_seconds;
#elif defined(RVL)
  BardXCReal time_seconds = OSTicksToMilliseconds(OSGetTime()) / 1000.0;
  return time_seconds;

#else
  struct timeval time_struct;
  BardXCReal time_seconds;
  gettimeofday( &time_struct, 0 );
  time_seconds = (BardXCReal) time_struct.tv_sec;
  time_seconds += (time_struct.tv_usec / 1000000.0);
  return time_seconds;
#endif
}

int BardUtil_hex_character_to_value( int ch )
{
  if (ch >= '0' && ch <= '9') return (ch - '0');
  if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
  if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
  return 0;
}

