//=============================================================================
//  Cargo.h
//
//  Created July 30, 2014 by Abe Pralle
//=============================================================================
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
//  CargoList
//=============================================================================
typedef struct CargoList
{
  void* data;
  int   count;
  int   capacity;
  int   element_size;
} CargoList;

#define CargoList_create( element_type ) \
  CargoList_create_with_element_size( sizeof(element_type), 10 )

#define CargoList_init( list, element_type ) \
  CargoList_init_with_element_size( list, sizeof(element_type), 10 )

#define CargoList_add( list, type, value ) \
  *((type*)(CargoList_new_last_element_pointer(list))) = (type) value

#define CargoList_get( list, type, index ) \
  ((type*)(list->data))[index]

CargoList* CargoList_create_with_element_size( int element_size, int initial_capacity );
CargoList* CargoList_init_with_element_size( CargoList* list, int element_size, int initial_capacity );
CargoList* CargoList_destroy( CargoList* list );
CargoList* CargoList_retire( CargoList* list );

void       CargoList_discard_at( CargoList* list, int index );
CargoList* CargoList_ensure_capacity( CargoList* list, int minimum_capacity );
void*      CargoList_new_last_element_pointer( CargoList* list );
void*      CargoList_pointer_at( CargoList* list, int index );
CargoList* CargoList_reserve( CargoList* list, int additional_spots );


//=============================================================================
//  CargoTable
//=============================================================================
typedef int  (*CargoTableMatchFn)( void* a, void* b );
typedef void (*CargoTableDeleteFn)( void* value );

typedef struct CargoTableEntry
{
  void* value;
  int   hash_code;
} CargoTableEntry;

typedef struct CargoTable
{
  CargoList** bins;

  int count;
  int bin_count;
  int bin_mask;
  int element_size;

  CargoTableMatchFn  match_fn;
  CargoTableDeleteFn delete_fn;
} CargoTable;

#define CargoTable_create( type, bin_count, match_fn, delete_fn ) \
  CargoTable_create_with_element_size( sizeof(type), bin_count, match_fn, delete_fn )

CargoTable* CargoTable_create_with_element_size(
    int element_size,
    int bin_count,
    CargoTableMatchFn  match_fn,
    CargoTableDeleteFn delete_fn
  );

CargoTable* CargoTable_init_with_element_size(
    CargoTable* table,
    int element_size,
    int bin_count,
    CargoTableMatchFn  match_fn,
    CargoTableDeleteFn delete_fn
  );

CargoTable* CargoTable_destroy( CargoTable* table );

CargoTable* CargoTable_retire( CargoTable* table );

//=============================================================================
//  Cargo
//=============================================================================
typedef struct Cargo
{
} Cargo;

Cargo* Cargo_create();
Cargo* Cargo_destroy( Cargo* cargo );

#ifdef __cplusplus
}; // end extern "C"
#endif

/*
{
  name:"on_pointer_press"
  x:32.2
  y:252.2
}
*/
