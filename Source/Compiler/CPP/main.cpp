#include "RogueTypes.h"
#include "RogueMM.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int* allocate( int size )
{
  int* result = (int*) RogueMM_allocator.allocate( size );
  memset( result, rand() & 255, size );
  *result = size;
  return result;
}

int* free( int* allocation )
{
  if ( !allocation ) return NULL;
  RogueMM_allocator.free( allocation, *allocation );
  return NULL;
}

int main()
{
  printf( "In main.cpp\n" );
  RogueMM mm;

  int** allocations = new int*[ 512 ];
  for (int times=0; times<10000; ++times)
  {
    int i = rand() & 511;
    allocations[i] = free( allocations[i] );
    allocations[i] = allocate( rand() & 511 );
  }

  printf( "Done\n" );

  return 0;
}
