#include <stdio.h>
#include "Rogue.h"

int main()
{
  RogueVM* vm = RogueVM_create();

  RogueVM_load_file( vm, "../RC2/Test.etc" );
  RogueVM_launch( vm );

  RogueVM_collect_garbage( vm );
  RogueVM_delete( vm );

  //RogueCmd* expression = RogueCmd_create_binary( &RogueCmdType_add_integer,
      //RogueCmd_create_literal_integer(3), RogueCmd_create_literal_integer(5) );
  //printf( "expression result: %d\n", expression->type->execute_integer(expression) );
  return 0;
}
