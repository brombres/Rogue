//=============================================================================
//  BardArray.c
//=============================================================================
#include <string.h>
#include "Bard.h"

BardArray* BardArray_create_of_type( struct BardVM* vm, BardType* array_type, BardType* element_type, int count )
{
  int element_size = element_type->reference_size;

  int data_size = element_size * count;
  int size = sizeof(BardArray) + data_size;
  // Even though BardArray already contains a single 64-bit element, we don't optimize
  // for that here to ensure compatibility with compound arrays, elements > 64 bits,
  // and associated padding issues.

  BardArray* array = (BardArray*) BardMM_create_object( &vm->mm, array_type, size );
  array->element_type = element_type;
  array->element_size = element_size;
  array->count = count;
  array->total_data_size = data_size;
  array->is_reference_array = BardType_is_reference( element_type );

  return array;
}

BardArray* BardArray_resized( BardArray* old_array, int new_count, int copy_count )
{
  int element_size = old_array->element_size;
  BardArray* new_array = BardArray_create_of_type( old_array->header.type->vm, old_array->header.type, 
      old_array->element_type, new_count );

  if (copy_count > 0)
  {
    memcpy( new_array->byte_data, old_array->byte_data, element_size*copy_count );
  }

  return new_array;
}

