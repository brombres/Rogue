//=============================================================================
//  BardList.c
//=============================================================================
#include <string.h>
#include "Bard.h"

BardObject*  BardObjectList_create( BardVM* vm, int initial_capacity )
{
  BardObject* list = BardType_create_object( vm->type_ObjectList );
  BardList_data(list) = BardArray_create_of_type( vm, vm->type_Array_of_Object, vm->type_Object, initial_capacity );
  return list;
}

BardObject*  BardRealList_create( BardVM* vm, int initial_capacity )
{
  BardObject* list = BardType_create_object( vm->type_RealList );
  BardList_data(list) = BardArray_create_of_type( vm, vm->type_Array_of_Real, vm->type_Real, initial_capacity );
  return list;
}

BardObject*  BardIntegerList_create( BardVM* vm, int initial_capacity )
{
  BardObject* list = BardType_create_object( vm->type_IntegerList );
  BardList_data(list) = BardArray_create_of_type( vm, vm->type_Array_of_Integer, vm->type_Integer, initial_capacity );
  return list;
}

BardObject*  BardCharacterList_create( BardVM* vm, int initial_capacity )
{
  BardObject* list = BardType_create_object( vm->type_CharacterList );
  BardList_data(list) = BardArray_create_of_type( vm, vm->type_Array_of_Character, vm->type_Character, initial_capacity );
  return list;
}

BardObject*  BardByteList_create( BardVM* vm, int initial_capacity )
{
  BardObject* list = BardType_create_object( vm->type_ByteList );
  BardList_data(list) = BardArray_create_of_type( vm, vm->type_Array_of_Byte, vm->type_Byte, initial_capacity );
  return list;
}

BardObject*  BardLogicalList_create( BardVM* vm, int initial_capacity )
{
  BardObject* list = BardType_create_object( vm->type_LogicalList );
  BardList_data(list) = BardArray_create_of_type( vm, vm->type_Array_of_Logical, vm->type_Logical, initial_capacity );
  return list;
}

void BardList_ensure_capacity_for_additional( BardObject* list, int additional_capacity )
{
  int required_capacity = BardList_count(list) + additional_capacity;
  int current_capacity = BardList_capacity( list );
  if (required_capacity > current_capacity)
  {
    int double_capacity = current_capacity * 2;
    if (double_capacity > required_capacity) required_capacity = double_capacity;
    BardList_ensure_capacity( list, required_capacity );
  }
}

void BardList_ensure_capacity( BardObject* list, int required_capacity )
{
  int current_capacity = BardList_capacity( list );
  if (current_capacity >= required_capacity) return;

  BardList_data(list) = BardArray_resized( BardList_data(list), required_capacity, BardList_count(list) );
}

