#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "Rogue.h"

int main()
{
  Rogue_program.configure();
  Rogue_program.launch();
  Rogue_program.collect_garbage();
  //printf( "Done\n" );

  return 0;
}

