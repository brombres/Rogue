#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "Rogue.h"

int main()
{
  printf( "Configuring Rogue\n" );

  rogue.init();
  RogueString::create( "Abe" );
  RogueString::create( "Abf" );
  RogueString::create( "Pralle" );
  rogue.collect_garbage();

  printf( "Done\n" );

  return 0;
}

