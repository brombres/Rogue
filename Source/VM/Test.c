#include <stdio.h>
#include "Rogue.h"

int main()
{
  RogueVM* vm = RogueVM_create();

  ROGUE_TRY(vm)
  {
    RogueTokenizer* tokenizer = RogueTokenizer_create_with_file( vm, ROGUE_STRING(vm,"Test.rogue") );
    RogueTokenizer_tokenize( tokenizer );

    printf( "-------------------------------------------------------------------------------\n" );
    while (RogueTokenizer_has_another(tokenizer))
    {
      RogueCmd* cmd = RogueTokenizer_read( tokenizer );
      cmd->type->print( cmd );
      printf("\n");
    }
    printf( "-------------------------------------------------------------------------------\n" );
  }
  ROGUE_CATCH(vm)
  {
    RogueVM_log_error( vm );
  }
  ROGUE_END_TRY(vm)

  RogueVM_delete( vm );

  //RogueCmd* expression = RogueCmd_create_binary( &RogueCmdType_add_integer,
      //RogueCmd_create_literal_integer(3), RogueCmd_create_literal_integer(5) );
  //printf( "expression result: %d\n", expression->type->execute_integer(expression) );
  return 0;
}
