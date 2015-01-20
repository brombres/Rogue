#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "RogueProgram.h"
#include "Rogue.h"
#include "RogueSystemList.h"
#include "RogueIDTable.h"

int main()
{
  printf( "Configuring Rogue\n" );

  rogue.init();
  RogueString* abe = RogueString::create( "Abe" );
  RogueString* abf = RogueString::create( "Abf" );
  RogueString* pralle = RogueString::create( "Pralle" );
  rogue.collect_garbage();

  printf( "Done\n" );

  return 0;
}

