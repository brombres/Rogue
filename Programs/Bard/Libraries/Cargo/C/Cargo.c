//=============================================================================
//  Cargo.c
//
//  Created July 30, 2014 by Abe Pralle
//=============================================================================
#include "Cargo.h"

#include <stdlib.h>
#include <string.h>

//=============================================================================
//  CargoList
//=============================================================================
CargoList* CargoList_create_with_element_size( int element_size, int initial_capacity )
{
  CargoList* list = (CargoList*) malloc( sizeof(CargoList) );
  return CargoList_init_with_element_size( list, element_size, initial_capacity );
}

CargoList* CargoList_init_with_element_size( CargoList* list, int element_size, int initial_capacity )
{
  list->count = 0;
  list->capacity = initial_capacity;
  list->element_size = element_size;

  int total_size = (element_size * initial_capacity);
  if (total_size) list->data = malloc( total_size );
  else            list->data = 0;

  return list;
}

CargoList* CargoList_destroy( CargoList* list )
{
  if (CargoList_retire(list))
  {
    free( list );
  }
  return 0;
}

CargoList* CargoList_retire( CargoList* list )
{
  if (list)
  {
    if (list->data)
    {
      free( list->data );
      list->data = 0;
    }
  }
  return list;
}

void CargoList_discard_at( CargoList* list, int index )
{
  int count = --list->count;

  if ((unsigned int) index > (unsigned int) count)
  {
    ++list->count;
    return;
  }

  if (index < count)
  {
    int element_size = list->element_size;
    memmove( list->data + (index*element_size), list->data + ((index+1)*element_size), (count-index)*element_size );
  }
}

CargoList* CargoList_ensure_capacity( CargoList* list, int minimum_capacity )
{
  int double_capacity;

  if (list->capacity >= minimum_capacity) return list;

  double_capacity = (list->capacity << 1);
  if (double_capacity > minimum_capacity) minimum_capacity = double_capacity;

  list->capacity = minimum_capacity;

  {
    int   e_size = list->element_size;
    void* old_data = list->data;
    list->data = malloc( minimum_capacity * e_size );
    if (old_data)
    {
      if (list->count) memcpy( list->data, old_data, list->count * e_size );
      free( old_data );
    }
  }

  return list;
}

CargoList* CargoList_reserve( CargoList* list, int additional_spots )
{
  return CargoList_ensure_capacity( list, list->count + additional_spots );
}

void* CargoList_new_last_element_pointer( CargoList* list )
{
  // Necessary because the CargoList_add() macro can't easily figure out
  // the last spot on its own.
  CargoList_reserve( list, 1 );
  return ((char*)list->data) + (list->count++ * list->element_size);
}

void* CargoList_pointer_at( CargoList* list, int index )
{
  return ((char*)list->data) + (index * list->element_size);
}

//=============================================================================
//  CargoTable
//=============================================================================
CargoTable* CargoTable_create_with_element_size(
    int element_size,
    int bin_count,
    CargoTableMatchFn  match_fn,
    CargoTableDeleteFn delete_fn
  )
{
  return CargoTable_init_with_element_size(
      (CargoTable*) malloc(sizeof(CargoTable)),
      element_size,
      bin_count,
      match_fn,
      delete_fn
    );
}

CargoTable* CargoTable_init_with_element_size(
    CargoTable* table,
    int element_size,
    int bin_count,
    CargoTableMatchFn  match_fn,
    CargoTableDeleteFn delete_fn
  )
{
  // Ensure bin_count is a power of two.
  int original_bin_count = bin_count;
  bin_count = 1;
  while (bin_count < original_bin_count) bin_count <<= 1;

  table->bin_count = bin_count;
  table->bin_mask = bin_count - 1;
  table->element_size = element_size;
  table->count = 0;
  table->match_fn = match_fn;
  table->delete_fn = delete_fn;

  table->bins = (CargoList**) malloc( bin_count * sizeof(CargoList*) );

  return table;
}

CargoTable* CargoTable_destroy( CargoTable* table )
{
  if ( !table ) return 0;
  free( CargoTable_retire(table) );
  return 0;
}

CargoTable* CargoTable_retire( CargoTable* table )
{
  if ( !table ) return 0;

  if (table->bins)
  {
      int i;
      for (i=0; i<table->bin_count; ++i)
      {
        CargoList* bin = table->bins[i];
        if (bin)
        {
          if (table->delete_fn)
          {
            while (bin->count > 0)
            {
              table->delete_fn( CargoList_pointer_at(bin, --bin->count) );
            }
          }
          table->bins[i] = CargoList_destroy( bin );
        }
        //CargoList_init_with_element_size( table->bins + i, element_size, 5 );
      }
    free( table->bins );
  }

  return table;
}

//-----------------------------------------------------------------------------
//  Cargo_ensure_capacity
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
//  Cargo_calculate_hash_code
//-----------------------------------------------------------------------------
int Cargo_calculate_hash_code( const char* st )
{
  int ch;
  int code = 0;

  --st;
  while ((ch = (unsigned char) *(++st)))
  {
    code = ((code<<3) - 1) + ch;
  }

  return code;
}

