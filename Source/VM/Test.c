#include <stdio.h>
#include "Rogue.h"

int main()
{
  printf( "-------------------------------------------------------------------------------\n" );

  RogueVM* vm = RogueVM_create();

  RogueString* st = RogueVM_consolidate_c_string( vm, "AB", -1 );
  printf( "hash: %d\n", st->object.type->hash_code(st) );
  printf( "equals: %d\n", st->object.type->equals_c_string(st,"AB") );
  printf( "equals: %d\n", st->object.type->equals_c_string(st,"A") );
  printf( "equals: %d\n", st->object.type->equals_c_string(st,"ABC") );

  RogueVM_delete( vm );

  //RogueCmd* expression = RogueCmd_create_binary( &RogueCmdType_add_integer,
      //RogueCmd_create_literal_integer(3), RogueCmd_create_literal_integer(5) );
  //printf( "expression result: %d\n", expression->type->execute_integer(expression) );

  printf( "-------------------------------------------------------------------------------\n" );
  return 0;
}
