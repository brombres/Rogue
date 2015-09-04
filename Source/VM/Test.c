#include <stdio.h>
#include "Rogue.h"

int main()
{
  printf( "-------------------------------------------------------------------------------\n" );

  RogueVM* vm = RogueVM_create();

  RogueParseReader* reader = RogueParseReader_create( vm, ROGUE_STRING(vm,"Test.c") );

  printf( "%6d  ", 1 );
  while (RogueParseReader_has_another(reader))
  {
    RogueCharacter ch = RogueParseReader_read(reader);
    putc( ch, stdout );
    if (ch == 10)
    {
      printf( "%6d  ", reader->position.line );
    }
  }

  RogueVM_delete( vm );

  //RogueCmd* expression = RogueCmd_create_binary( &RogueCmdType_add_integer,
      //RogueCmd_create_literal_integer(3), RogueCmd_create_literal_integer(5) );
  //printf( "expression result: %d\n", expression->type->execute_integer(expression) );

  printf( "-------------------------------------------------------------------------------\n" );
  return 0;
}
