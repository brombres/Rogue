#include <stdio.h>
#include "Rogue.h"

int main()
{
  printf( "+Rogue Test\n" );

  RogueVM* vm = RogueVM_create();

  RogueString* st = RogueString_create_from_utf8( vm, "abc", -1 );

  RogueString_log( st );
  printf("\n");

  RogueVM_delete( vm );

  //RogueCmd* expression = RogueCmd_create_binary( &RogueCmdType_add_integer,
      //RogueCmd_create_literal_integer(3), RogueCmd_create_literal_integer(5) );
  //printf( "expression result: %d\n", expression->type->execute_integer(expression) );

  printf( "-Rogue Test\n" );
  return 0;
}
