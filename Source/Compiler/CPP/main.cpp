#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "Test.h"
#include "RogueSystemList.h"
#include "RogueIDTable.h"

int main()
{
  printf( "Configuring Rogue\n" );
  Rogue_configure();
  printf( "bytes: %d\n", RogueType_Alpha->object_size );
  printf( "Done\n" );

  return 0;
}
