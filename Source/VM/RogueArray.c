//=============================================================================
//  RogueArray.c
//
//  2015.09.02 by Abe Pralle
//=============================================================================
#include "Rogue.h"

RogueInteger RogueByteArray_intrinsic_fn( RogueIntrinsicFnType fn_type,
    RogueObject* context, void* parameter )
{
  switch (fn_type)
  {
    case ROGUE_INTRINSIC_FN_TRACE:
      return 0;

    case ROGUE_INTRINSIC_FN_HASH_CODE:
      return ((RogueArray*)context)->count;

    case ROGUE_INTRINSIC_FN_TO_STRING:
    {
      RogueStringBuilder* builder = parameter;
      RogueStringBuilder_print_c_string_with_count( builder, 
          (char*) ((RogueArray*)context)->bytes,
          ((RogueArray*)context)->count
        );
      return 0;
    }

    case ROGUE_INTRINSIC_FN_EQUALS_OBJECT:
      printf( "TODO: RogueByteArray_intrinsic_fn EQUALS_OBJECT\n" );
      return 0;

    case ROGUE_INTRINSIC_FN_EQUALS_C_STRING:
      printf( "TODO: RogueByteArray_intrinsic_fn EQUALS_C_STRING\n" );
      return 0;

    case ROGUE_INTRINSIC_FN_EQUALS_CHARACTERS:
      printf( "TODO: RogueByteArray_intrinsic_fn EQUALS_CHARACTERS\n" );
      return 0;
  }

  return 0;
}

RogueInteger RogueCharacterArray_intrinsic_fn( RogueIntrinsicFnType fn_type,
    RogueObject* context, void* parameter )
{
  switch (fn_type)
  {
    case ROGUE_INTRINSIC_FN_TRACE:
      return 0;

    case ROGUE_INTRINSIC_FN_HASH_CODE:
      return ((RogueArray*)context)->count;

    case ROGUE_INTRINSIC_FN_TO_STRING:
    {
      RogueStringBuilder* builder = parameter;
      RogueStringBuilder_print_characters( builder, 
          ((RogueArray*)context)->characters,
          ((RogueArray*)context)->count
        );
      return 0;
    }

    case ROGUE_INTRINSIC_FN_EQUALS_OBJECT:
      printf( "TODO: RogueCharacterArray_intrinsic_fn EQUALS_OBJECT\n" );
      return 0;

    case ROGUE_INTRINSIC_FN_EQUALS_C_STRING:
      printf( "TODO: RogueCharacterArray_intrinsic_fn EQUALS_C_STRING\n" );
      return 0;

    case ROGUE_INTRINSIC_FN_EQUALS_CHARACTERS:
      printf( "TODO: RogueCharacterArray_intrinsic_fn EQUALS_CHARACTERS\n" );
      return 0;
  }

  return 0;
}

RogueInteger RogueObjectArray_intrinsic_fn( RogueIntrinsicFnType fn_type,
    RogueObject* context, void* parameter )
{
  switch (fn_type)
  {
    case ROGUE_INTRINSIC_FN_TRACE:
      printf( "TODO: RogueObjectArray TRACE\n" );
      return 0;

    case ROGUE_INTRINSIC_FN_HASH_CODE:
      printf( "TODO: RogueObjectArray HASH_CODE\n" );
      return 0;

    case ROGUE_INTRINSIC_FN_TO_STRING:
    {
      RogueStringBuilder* builder = parameter;
      RogueArray* array = (RogueArray*) context;
      int i;

      RogueStringBuilder_print_character( builder, '[' );
      for (i=0; i<array->count; ++i)
      {
        RogueObject* object = array->objects[i];
        if (i > 0) RogueStringBuilder_print_character( builder, ',' );

        if (object) RogueObject_to_string( object, builder );
        else        RogueStringBuilder_print_c_string( builder, "null" );
      }
      RogueStringBuilder_print_character( builder, ']' );
      return 0;
    }

    case ROGUE_INTRINSIC_FN_EQUALS_OBJECT:
      printf( "TODO: RogueCharacterArray_intrinsic_fn EQUALS_OBJECT\n" );
      return 0;

    case ROGUE_INTRINSIC_FN_EQUALS_C_STRING:
      printf( "TODO: RogueCharacterArray_intrinsic_fn EQUALS_C_STRING\n" );
      return 0;

    case ROGUE_INTRINSIC_FN_EQUALS_CHARACTERS:
      printf( "TODO: RogueCharacterArray_intrinsic_fn EQUALS_CHARACTERS\n" );
      return 0;
  }

  return 0;
}

RogueType* RogueTypeByteArray_create( RogueVM* vm )
{
  RogueType* THIS = RogueVM_create_type( vm, 0, "ByteArray", sizeof(RogueArray) );
  THIS->element_size = sizeof(RogueByte);
  THIS->intrinsic_fn = RogueByteArray_intrinsic_fn;
  return THIS;
}

RogueType* RogueTypeCharacterArray_create( RogueVM* vm )
{
  RogueType* THIS = RogueVM_create_type( vm, 0, "CharacterArray", sizeof(RogueArray) );
  THIS->element_size = sizeof(RogueCharacter);
  THIS->intrinsic_fn = RogueCharacterArray_intrinsic_fn;
  return THIS;
}

RogueType* RogueTypeObjectArray_create( RogueVM* vm )
{
  RogueType* THIS = RogueVM_create_type( vm, 0, "ObjectArray", sizeof(RogueArray) );
  THIS->element_size = sizeof(RogueObject*);
  THIS->intrinsic_fn = RogueObjectArray_intrinsic_fn;
  return THIS;
}

RogueArray* RogueArray_create( RogueType* array_type, RogueInteger count )
{
  RogueArray* THIS;

  if (count < 0) count = 0;

  THIS = (RogueArray*) RogueObject_create( array_type,
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

