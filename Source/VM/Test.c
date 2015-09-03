#include <stdio.h>
#include "Rogue.h"

int main()
{
  printf( "-------------------------------------------------------------------------------\n" );

  RogueVM* vm = RogueVM_create();

  RogueList* list = RogueList_create( vm->type_ObjectList, 1);

  RogueList_add_object( list, RogueVM_consolidate_utf8(vm,"one",-1) );
  RogueList_add_object( list, RogueVM_consolidate_utf8(vm,"two",-1) );
  RogueList_add_object( list, RogueVM_consolidate_utf8(vm,"three",-1) );
  RogueList_add_object( list, RogueVM_consolidate_utf8(vm,"four",-1) );
  RogueList_add_object( list, RogueVM_consolidate_utf8(vm,"five",-1) );

  RogueObject_println( list );

  RogueVM_delete( vm );

  //RogueCmd* expression = RogueCmd_create_binary( &RogueCmdType_add_integer,
      //RogueCmd_create_literal_integer(3), RogueCmd_create_literal_integer(5) );
  //printf( "expression result: %d\n", expression->type->execute_integer(expression) );

  printf( "-------------------------------------------------------------------------------\n" );
  return 0;
}
