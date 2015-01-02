#include "SuperC.h"
#include "SuperCFileReader.h"
#include "SuperCStringBuilder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
//  main
//=============================================================================
int main()
{
  printf( "+SuperC\n" );
  SuperC* super_c = SuperC_create();

  SuperCStringBuilder* buffer = SuperCStringBuilder_create( super_c );
  SuperCFileReader* reader = SuperCFileReader_create( super_c, "Test.sc" );
  while (reader->position < reader->count)
  {
    SuperCStringBuilder_print_character( buffer, reader->characters[reader->position++] );
  }
  SuperCFileReader_destroy( super_c, reader );

  printf( "%s", SuperCStringBuilder_to_c_string(buffer) );

  buffer = SuperCStringBuilder_destroy( buffer );

  super_c = SuperC_destroy( super_c );
  printf( "-SuperC\n" );
  return 0;
}


//=============================================================================
//  SuperC
//=============================================================================
SuperC* SuperC_create()
{
  SuperC* super_c = (SuperC*) malloc( sizeof(SuperC) );
  memset( super_c, 0, sizeof(SuperC) );
  return super_c;
}


SuperC* SuperC_destroy( SuperC* super_c )
{
  if (super_c)
  {
    SuperCAllocation* cur = super_c->allocations;
    while (cur)
    {
      SuperCAllocation* next = cur->next_allocation;
      free( cur );
      cur = next;
    }
    super_c->allocations = 0;

    free( super_c );
  }
  return 0;
}


void* SuperC_allocate( SuperC* super_c, int size )
{
  int total_size = (sizeof(SuperCAllocation) - sizeof(double)) + size;

  SuperCAllocation* allocation = malloc( total_size );

  memset( allocation, 0, total_size );
  if (super_c->allocations) super_c->allocations->previous_allocation = allocation;
  allocation->next_allocation = super_c->allocations;
  super_c->allocations = allocation;

  return allocation->data;
}

void* SuperC_free( SuperC* super_c, void* data )
{
  SuperCAllocation* allocation;
  SuperCAllocation* previous_allocation;
  SuperCAllocation* next_allocation;

  if ( !data) return 0;

  allocation = (SuperCAllocation*) data;

  // Scoot the allocation pointer back to the allocation header.
  allocation = (SuperCAllocation*) (((char*) allocation) + ((int)(((char*)allocation) - ((char*) allocation->data))));

  previous_allocation = allocation->previous_allocation;
  next_allocation = allocation->next_allocation;

  if (next_allocation) next_allocation->previous_allocation = previous_allocation;

  if (previous_allocation)
  {
    previous_allocation->next_allocation = next_allocation;
  }
  else
  {
    super_c->allocations = next_allocation;
  }

  free( allocation );

  return 0;
}

char* SuperC_clone_c_string( SuperC* super_c, const char* st )
{
  int len = (int) strlen(st);
  char* data = (char*) SuperC_allocate( super_c, len+1 );
  strcpy( data, st );
  return data;
}

