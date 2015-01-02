#include "SuperFileSystem.h"
#include <stdio.h>
#include <stdlib.h>

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================
char* SuperFileSystem_load( const char* filepath )
{
  int size;
  char* buffer;
  FILE* fp = fopen( filepath, "rb" );
  if ( !fp ) return NULL;

  fseek( fp, 0, SEEK_END );
  size = (int) ftell( fp );
  fseek( fp, 0, SEEK_SET );
  if (size <= 0) return NULL;

  buffer = SUPER_ALLOCATE( size );
  fread( buffer, 1, size, fp );

  fclose( fp );

  return buffer;
}

int SuperFileSystem_size( const char* filepath )
{
  int size;
  FILE* fp = fopen( filepath, "rb" );
  if ( !fp ) return 0;

  fseek( fp, 0, SEEK_END );
  size = (int) ftell( fp );
  fclose( fp );

  return size;
}

