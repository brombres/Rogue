//=============================================================================
//  RogueList.c
//
//  2015.09.02 by Abe Pralle
//=============================================================================
#include "Rogue.h"

RogueType* RogueTypeObjectList_create( RogueVM* vm )
{
  RogueType* THIS = RogueType_create( vm, "Object[]", sizeof(RogueList) );
  THIS->element_size = sizeof(RogueObject*);
  THIS->print = RoguePrintFn_object_list;
  return THIS;
}

void RoguePrintFn_object_list( void* list_ptr, RogueStringBuilder* builder )
{
  RogueList* list = list_ptr;
  int i;

  RogueStringBuilder_print_character( builder, '[' );
  for (i=0; i<list->count; ++i)
  {
    RogueObject* object = list->array->objects[i];
    if (i > 0) RogueStringBuilder_print_character( builder, ',' );

    if (object) object->type->print( object, builder );
    else        RogueStringBuilder_print_c_string( builder, "null" );
  }
  RogueStringBuilder_print_character( builder, ']' );
}

RogueList* RogueList_create( RogueType* list_type, RogueInteger initial_capacity )
{
  RogueList* THIS = (RogueList*) RogueType_create_object( list_type, -1 );
  THIS->array = RogueArray_create( list_type->vm->type_ObjectArray, initial_capacity );
  return THIS;
}

RogueList* RogueObjectList_create( RogueVM* vm, RogueInteger initial_capacity )
{
  return RogueList_create( vm->type_ObjectList, initial_capacity );
}

RogueList* RogueList_add_object( RogueList* THIS, void* object )
{
  RogueList_reserve( THIS, 1 );
  THIS->array->objects[ THIS->count++ ] = object;
  return THIS;
}

RogueList* RogueList_reserve( RogueList* THIS, RogueInteger additional_capacity )
{
  RogueInteger required_capacity = THIS->count + additional_capacity;
  RogueInteger double_capacity;
  RogueArray* new_array;

  if (required_capacity <= THIS->array->count) return THIS;

  double_capacity = THIS->array->count << 1;
  if (double_capacity > required_capacity) required_capacity = double_capacity;

  new_array = RogueArray_create( THIS->array->object.type, required_capacity );
  memcpy( new_array->bytes, THIS->array->bytes, THIS->count * THIS->object.type->element_size );
  THIS->array = new_array;

  return THIS;
}

