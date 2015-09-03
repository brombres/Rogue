//=============================================================================
//  RogueObject.c
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#include "Rogue.h"

void RogueObject_print( void* THIS )
{
  RogueStringBuilder builder;
  RogueStringBuilder_init( &builder, -1 );

  ((RogueObject*)THIS)->type->print( THIS, &builder );

  RogueStringBuilder_log( &builder );
}

void RogueObject_println( void* THIS )
{
  RogueObject_print( THIS );
  printf( "\n" );
}

