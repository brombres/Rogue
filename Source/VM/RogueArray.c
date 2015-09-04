//=============================================================================
//  RogueArray.c
//
//  2015.09.02 by Abe Pralle
//=============================================================================
#include "Rogue.h"

void RoguePrintFn_byte_array( void* array_ptr, RogueStringBuilder* builder )
{
  RogueArray* array = array_ptr;
  RogueStringBuilder_print_c_string_with_count( builder, (char*) array->bytes, array->count );
}

void RoguePrintFn_character_array( void* array_ptr, RogueStringBuilder* builder )
{
  RogueArray* array = array_ptr;
  RogueStringBuilder_print_characters( builder, array->characters, array->count );
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

RogueType* RogueTypeByteArray_create( RogueVM* vm )
{
  RogueType* THIS = RogueType_create( vm, "ByteArray", sizeof(RogueArray) );
  THIS->element_size = sizeof(RogueByte);
  THIS->print = RoguePrintFn_byte_array;
  return THIS;
}

RogueType* RogueTypeCharacterArray_create( RogueVM* vm )
{
  RogueType* THIS = RogueType_create( vm, "CharacterArray", sizeof(RogueArray) );
  THIS->element_size = sizeof(RogueCharacter);
  THIS->print = RoguePrintFn_character_array;
  return THIS;
}

RogueType* RogueTypeObjectArray_create( RogueVM* vm )
{
  RogueType* THIS = RogueType_create( vm, "ObjectArray", sizeof(RogueArray) );
  THIS->element_size = sizeof(RogueObject*);
  THIS->print = RoguePrintFn_object_array;
  return THIS;
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

RogueArray* RogueByteArray_create( RogueVM* vm, RogueInteger count )
{
  return RogueArray_create( vm->type_ByteArray, count );
}

RogueArray* RogueCharacterArray_create( RogueVM* vm, RogueInteger count )
{
  return RogueArray_create( vm->type_CharacterArray, count );
}

RogueArray* RogueObjectArray_create( RogueVM* vm, RogueInteger count )
{
  return RogueArray_create( vm->type_ObjectArray, count );
}

