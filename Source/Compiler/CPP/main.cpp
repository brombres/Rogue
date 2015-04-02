#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "Rogue.h"

int main( int argc, char* argv[] )
{
  Rogue_program.configure( argc, argv );
  Rogue_program.launch();
  Rogue_program.collect_garbage();
  //printf( "Done\n" );

  return 0;
}

