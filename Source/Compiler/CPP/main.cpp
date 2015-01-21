#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "Rogue.h"

int main()
{
  rogue_program.init();
  rogue_program.collect_garbage();
  //printf( "Done\n" );

  return 0;
}

