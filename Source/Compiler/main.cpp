#include "RogueTypes.h"
#include "RogueMM.h"
#include <stdio.h>

int main()
{
  printf( "In main.cpp\n" );
  RogueMM mm;

  for (int i=0; i<258; ++i)
  {
    mm.get_allocator(i)->allocate(i);
  }

  return 0;
}
