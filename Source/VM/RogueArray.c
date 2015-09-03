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
  THIS->print = RoguePrintFn_object_array;
  return THIS;
}

void RoguePrintFn_object_array( void* array_ptr, RogueStringBuilder* builder )
{
  RogueArray* array = array_ptr;
  int i;

  RogueStringBuilder_print_character( builder, '[' );
  for (i=0; i<array->count; ++i)
  {
    RogueObject* object = array->objects[i];
    if (i > 0) RogueStringBuilder_print_character( builder, ',' );

    if (object) object->type->print( object, builder );
    else        RogueStringBuilder_print_c_string( builder, "null" );
  }
  RogueStringBuilder_print_character( builder, ']' );
}

RogueArray* RogueArray_create( RogueType* array_type, RogueInteger count )
{
  RogueArray* THIS;

  if (count < 0) count = 0;

  THIS = (RogueArray*) RogueType_create_object( array_type,
      sizeof(RogueArray) + count * array_type->element_size );
  THIS->count = count;
  return THIS;
}
