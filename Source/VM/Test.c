#include <stdio.h>
#include "Rogue.h"

int main()
{
  printf( "+Rogue Test\n" );

  RogueCmd* expression = RogueCmd_create_binary( &RogueCmdType_add_integer,
      RogueCmd_create_literal_integer(3), RogueCmd_create_literal_integer(5) );

  printf( "expression result: %d\n", expression->type->execute_integer(expression) );

  printf( "-Rogue Test\n" );
  return 0;
}
