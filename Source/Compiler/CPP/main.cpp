#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "Test.h"
#include "RogueSystemList.h"
#include "RogueIDTable.h"

void print( const char* st )
{
  printf( "%s:%d\n", st, Rogue_id_table[st] );
}

int main()
{
  printf( "In main.cpp\n" );

  print( "Mary" );
  print( "had" );
  print( "a" );
  print( "little" );
  print( "lamb" );
  print( "little" );
  print( "lamb" );
  print( "little" );
  print( "lamb" );
  print( "Mary" );
  print( "had" );
  print( "a" );
  print( "little" );
  print( "lamb" );
  print( "his" );
  print( "fleece" );
  print( "was" );
  print( "white" );
  print( "as" );
  print( "snow" );
  printf( "\n" );

  for (int i=1; i<=Rogue_id_table.count; ++i)
  {
    printf( "%d:%s\n", i, Rogue_id_table[i] );
  }

  printf( "Done\n" );

  return 0;
}
