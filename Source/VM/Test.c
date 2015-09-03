#include <stdio.h>
#include "Rogue.h"

int main()
{
  printf( "-------------------------------------------------------------------------------\n" );

  RogueVM* vm = RogueVM_create();

  RogueTable* table = RogueTable_create( vm, 5 );

  RogueObject_println( table );

  RogueVM_delete( vm );

  //RogueCmd* expression = RogueCmd_create_binary( &RogueCmdType_add_integer,
      //RogueCmd_create_literal_integer(3), RogueCmd_create_literal_integer(5) );
  //printf( "expression result: %d\n", expression->type->execute_integer(expression) );

  printf( "-------------------------------------------------------------------------------\n" );
  return 0;
}
