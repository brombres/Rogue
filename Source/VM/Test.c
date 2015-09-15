#include <stdio.h>
#include "Rogue.h"

int main()
{
  RogueVM* vm = RogueVM_create();

  //RogueVM_load_file( vm, "../RC2/Test.etc" );

  ROGUE_TRY(vm)
  {
    RogueETCReader* reader = RogueETCReader_create_with_file( vm, ROGUE_STRING(vm,"../RC2/Test.etc") );

    printf( "-------------------------------------------------------------------------------\n" );
    printf( "%c", RogueETCReader_read_byte(reader) );
    printf( "%c", RogueETCReader_read_byte(reader) );
    printf( "%c", RogueETCReader_read_byte(reader) );
    printf( "\nVersion %d\n", RogueETCReader_read_integer_x(reader) );
    printf( "-------------------------------------------------------------------------------\n" );
  }
  ROGUE_CATCH(vm)
  {
    RogueVM_log_error( vm );
    return 1;
  }
  ROGUE_END_TRY(vm)

  RogueVM_collect_garbage( vm );
  RogueVM_delete( vm );

  //RogueCmd* expression = RogueCmd_create_binary( &RogueCmdType_add_integer,
      //RogueCmd_create_literal_integer(3), RogueCmd_create_literal_integer(5) );
  //printf( "expression result: %d\n", expression->type->execute_integer(expression) );
  return 0;
}
