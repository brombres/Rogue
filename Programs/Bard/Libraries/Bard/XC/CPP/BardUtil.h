#ifndef BARD_UTIL_H
#define BARD_UTIL_H

#include <setjmp.h>

//=============================================================================
//  Macros
//=============================================================================
#define BARD_TRY(vm) { jmp_buf local_jump_buf; vm->current_error_handler = &local_jump_buf; if ( !setjmp(local_jump_buf) ) {
#define BARD_CATCH_ERROR } else {
#define BARD_END_TRY(vm) } vm->current_error_handler = NULL; }


//=============================================================================
//  Memory Allocation
//=============================================================================
void* Bard_allocate_bytes_and_clear( int size ); // Supports convenience macro Bard_allocate(type)
void* Bard_free( void* ptr );

//=============================================================================
//  BardVMCString
//=============================================================================
int   BardVMCString_hash_code( const char* st );
int   BardVMCString_find( const char* st, char ch );
char* BardVMCString_duplicate( const char* st );
 

//=============================================================================
//  BardHashTable
//=============================================================================
typedef struct BardHashTableEntry
{
  struct BardHashTableEntry* next_entry;
  char*  key;
  void*  value;
  int    hash_code;
} BardHashTableEntry;

typedef struct BardHashTable
{
  BardHashTableEntry** bins;
  int                    bin_mask;
} BardHashTable;

BardHashTable* BardHashTable_create( int bin_count );
BardHashTable* BardHashTable_destroy( BardHashTable* table );

void* BardHashTable_get( BardHashTable* table, const char* key );
void  BardHashTable_set( BardHashTable* table, const char* key, void* value );

//=============================================================================
//  BardIntList
//=============================================================================
typedef struct BardIntList
{
  int*   data;
  int    capacity;
  int    count;
} BardIntList;

BardIntList* BardIntList_create();
BardIntList* BardIntList_destroy( BardIntList* list );

BardIntList* BardIntList_init( BardIntList* list );
void           BardIntList_release( BardIntList* list );

BardIntList* BardIntList_clear( BardIntList* list );

void BardIntList_ensure_capacity( BardIntList* list, int min_capacity );
void BardIntList_add( BardIntList* list, int value );
int  BardIntList_remove_last( BardIntList* list );


//=============================================================================
//  BardPointerList
//=============================================================================
typedef struct BardPointerList
{
  void** data;
  int    capacity;
  int    count;
} BardPointerList;

BardPointerList* BardPointerList_create();
void             BardPointerList_destroy( BardPointerList* list );

BardPointerList* BardPointerList_init( BardPointerList* list );
void             BardPointerList_release( BardPointerList* list );

BardPointerList* BardPointerList_clear( BardPointerList* list );
BardPointerList* BardPointerList_free_elements( BardPointerList* list );

void  BardPointerList_ensure_capacity( BardPointerList* list, int min_capacity );
void  BardPointerList_add( BardPointerList* list, void* value );


//=============================================================================
//  BardResourceIDTable
//  Looks up resource IDs using void* keys.
//=============================================================================
typedef struct BardResourceIDTableEntry
{
  struct BardResourceIDTableEntry* next_entry;
  void*  key;
  int    value;
} BardResourceIDTableEntry;

typedef struct BardResourceIDTable
{
  BardResourceIDTableEntry** bins;
  int                    bin_mask;
} BardResourceIDTable;

BardResourceIDTable* BardResourceIDTable_create( int bin_count );
BardResourceIDTable* BardResourceIDTable_destroy( BardResourceIDTable* table );

int  BardResourceIDTable_get( BardResourceIDTable* table, void* key );
void BardResourceIDTable_set( BardResourceIDTable* table, void* key, int value );


//=============================================================================
//  BardResourceBank
//=============================================================================
typedef struct BardResourceBank
{
  BardPointerList resources;
  BardIntList     available_indices;
  int             initialized;
} BardResourceBank;

BardResourceBank* BardResourceBank_create();
BardResourceBank* BardResourceBank_destroy( BardResourceBank* bank );

void BardResourceBank_init( BardResourceBank* bank );
void BardResourceBank_release( BardResourceBank* bank );

int   BardResourceBank_add( BardResourceBank* bank, void* data );
void  BardResourceBank_clear( BardResourceBank* bank );
int   BardResourceBank_create_id( BardResourceBank* bank );
int   BardResourceBank_find( BardResourceBank* bank, void* data );
int   BardResourceBank_find_first( BardResourceBank* bank );
void* BardResourceBank_get( BardResourceBank* bank, int id );
void* BardResourceBank_remove( BardResourceBank* bank, int id );
void  BardResourceBank_set( BardResourceBank* bank, int id, void* data );


//=============================================================================
//  BardUtil
//=============================================================================
BardXCReal BardUtil_time();
int      BardUtil_hex_character_to_value( int ch );

#endif // BARD_UTIL_H

