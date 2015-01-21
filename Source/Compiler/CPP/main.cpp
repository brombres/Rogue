#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "Rogue.h"

int main()
{
  printf( "Configuring Rogue\n" );

  rogue.init();
  rogue.collect_garbage();

  printf( "Done\n" );

  return 0;
}

