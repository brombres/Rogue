//=============================================================================
//  RogueList.c
//
//  2015.09.02 by Abe Pralle
//=============================================================================
#include "Rogue.h"

//-----------------------------------------------------------------------------
//  List Types
//-----------------------------------------------------------------------------
RogueInteger RogueByteList_intrinsic_fn( RogueIntrinsicFnType fn_type,
    RogueObject* context, void* parameter )
{
  switch (fn_type)
  {
    case ROGUE_INTRINSIC_FN_TRACE:
      return 0;

    case ROGUE_INTRINSIC_FN_HASH_CODE:
      return ((RogueList*)context)->count;

    case ROGUE_INTRINSIC_FN_TO_STRING:
    {
      RogueStringBuilder* builder = parameter;
      RogueStringBuilder_print_c_string_with_count( builder, 
          (char*) ((RogueList*)context)->array->bytes,
          ((RogueList*)context)->count
        );
      return 0;
    }

    case ROGUE_INTRINSIC_FN_EQUALS_OBJECT:
      printf( "TODO: RogueByteList_intrinsic_fn EQUALS_OBJECT\n" );
      return 0;

    case ROGUE_INTRINSIC_FN_EQUALS_C_STRING:
      printf( "TODO: RogueByteList_intrinsic_fn EQUALS_C_STRING\n" );
      return 0;

    case ROGUE_INTRINSIC_FN_EQUALS_CHARACTERS:
      printf( "TODO: RogueByteList_intrinsic_fn EQUALS_CHARACTERS\n" );
      return 0;
  }

  return 0;
}

RogueInteger RogueObjectList_intrinsic_fn( RogueIntrinsicFnType fn_type,
    RogueObject* context, void* parameter )
{
  switch (fn_type)
  {
    case ROGUE_INTRINSIC_FN_TRACE:
      printf( "TODO: RogueObjectList TRACE\n" );
      return 0;

    case ROGUE_INTRINSIC_FN_HASH_CODE:
      printf( "TODO: RogueObjectList HASH_CODE\n" );
      return 0;

    case ROGUE_INTRINSIC_FN_TO_STRING:
    {
      RogueStringBuilder* builder = parameter;
      RogueList* list = (RogueList*) context;
      int i;

      RogueStringBuilder_print_character( builder, '[' );
      for (i=0; i<list->count; ++i)
      {
        RogueObject* object = list->array->objects[i];
        if (i > 0) RogueStringBuilder_print_character( builder, ',' );

        if (object) RogueObject_to_string( object, builder );
        else        RogueStringBuilder_print_c_string( builder, "null" );
      }
      RogueStringBuilder_print_character( builder, ']' );
      return 0;
    }

    case ROGUE_INTRINSIC_FN_EQUALS_OBJECT:
      printf( "TODO: RogueCharacterList_intrinsic_fn EQUALS_OBJECT\n" );
      return 0;

    case ROGUE_INTRINSIC_FN_EQUALS_C_STRING:
      printf( "TODO: RogueCharacterList_intrinsic_fn EQUALS_C_STRING\n" );
      return 0;

    case ROGUE_INTRINSIC_FN_EQUALS_CHARACTERS:
      printf( "TODO: RogueCharacterList_intrinsic_fn EQUALS_CHARACTERS\n" );
      return 0;
  }

  return 0;
}

RogueType* RogueTypeByteList_create( RogueVM* vm )
{
  RogueType* THIS = RogueVM_create_type( vm, 0, "Byte[]", sizeof(RogueList) );
  THIS->element_size = sizeof(RogueByte);
  THIS->intrinsic_fn = RogueByteList_intrinsic_fn;
  return THIS;
}

RogueType* RogueTypeObjectList_create( RogueVM* vm )
{
  RogueType* THIS = RogueVM_create_type( vm, 0, "Object[]", sizeof(RogueList) );
  THIS->element_size = sizeof(RogueObject*);
  THIS->intrinsic_fn = RogueObjectList_intrinsic_fn;
  return THIS;
}

//-----------------------------------------------------------------------------
//  List Objects
//-----------------------------------------------------------------------------
RogueList* RogueList_create( RogueType* list_type, RogueType* array_type, RogueInteger initial_capacity )
{
  RogueList* THIS = (RogueList*) RogueObject_create( list_type, -1 );
  THIS->array = RogueArray_create( array_type, initial_capacity );
  return THIS;
}

RogueList* RogueByteList_create( RogueVM* vm, RogueInteger initial_capacity )
{
  return RogueList_create( vm->type_ByteList, vm->type_ByteArray, initial_capacity );
}

RogueList* RogueObjectList_create( RogueVM* vm, RogueInteger initial_capacity )
{
  return RogueList_create( vm->type_ObjectList, vm->type_ObjectArray, initial_capacity );
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

RogueList* RogueList_resize( RogueList* THIS, RogueInteger new_count )
{
  RogueInteger diff = new_count - THIS->count;
  if (diff > 0)
  {
    RogueList_reserve( THIS, diff );
  }
  else if (diff < 0)
  {
    int element_size = THIS->array->object.type->element_size;
    memset( THIS->array->bytes + new_count*element_size, 0, -diff * element_size );
  }

  THIS->count = new_count;

  return THIS;
}

