#include <stdio.h>
#include "Rogue.h"

int main()
{
  printf( "-------------------------------------------------------------------------------\n" );

  RogueVM* vm = RogueVM_create();

  RogueArray* array = RogueArray_create( vm->type_ObjectArray, 5 );
  printf( "Array count: %d\n", array->count );

  array->objects[0] = (RogueObject*) RogueVM_consolidate_utf8( vm, "zero", -1 );
  array->objects[1] = (RogueObject*) RogueVM_consolidate_utf8( vm, "one", -1 );
  array->objects[2] = (RogueObject*) RogueVM_consolidate_utf8( vm, "two", -1 );
  array->objects[3] = (RogueObject*) RogueVM_consolidate_utf8( vm, "three", -1 );
  array->objects[4] = (RogueObject*) RogueVM_consolidate_utf8( vm, "four", -1 );

  {
    int i;
    for (i=0; i<array->count; ++i)
    {
      RogueString_log( (RogueString*) array->objects[i] );
      printf("\n");
    }
  }

  RogueVM_delete( vm );

  //RogueCmd* expression = RogueCmd_create_binary( &RogueCmdType_add_integer,
      //RogueCmd_create_literal_integer(3), RogueCmd_create_literal_integer(5) );
  //printf( "expression result: %d\n", expression->type->execute_integer(expression) );

  printf( "-------------------------------------------------------------------------------\n" );
  return 0;
}
