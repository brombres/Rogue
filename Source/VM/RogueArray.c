//=============================================================================
//  RogueArray.c
//
//  2015.09.02 by Abe Pralle
//=============================================================================
#include "Rogue.h"

RogueType* RogueTypeObjectArray_create( RogueVM* vm )
{
  RogueType* THIS = RogueType_create( vm, "ObjectArray", sizeof(RogueArray) );
  THIS->element_size = sizeof(RogueObject*);
  return THIS;
}

RogueArray* RogueArray_create( RogueType* array_type, RogueInteger count )
{
  RogueArray* THIS = (RogueArray*) RogueType_create_object( array_type,
      sizeof(RogueArray) + count * array_type->element_size );
  THIS->count = count;
  return THIS;
}
