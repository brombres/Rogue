//=============================================================================
//  RogueType.c
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#include "Rogue.h"

RogueType* RogueType_create( RogueVM* vm )
{
  RogueType* THIS = (RogueType*) malloc( sizeof(RogueType) );
  THIS->vm = vm;
  return THIS;
}

