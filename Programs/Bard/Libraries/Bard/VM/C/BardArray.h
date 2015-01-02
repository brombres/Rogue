#ifndef BARD_ARRAY_H
#define BARD_ARRAY_H
//=============================================================================
//  BardArray.h
//
//  BardArray : BardObject
//=============================================================================

typedef struct BardArray
{
  BardObject header;

  // BardArray parts
  BardType*          element_type;
  BardInteger        count;
  BardInteger        element_size;
  BardInteger        total_data_size;  // in bytes
  BardInteger        is_reference_array;

  union
  {
    unsigned char    byte_data[1];
    BardCharacter    character_data[1];
    BardInteger      integer_data[1];
    BardReal         real_data[1];   // Guarantee 8-byte array alignment
    BardObject*      object_data[1];
    BardVMStackValue   slot_data[1];
  };
} BardArray;

BardArray* BardArray_create_of_type( struct BardVM* vm, BardType* array_type, BardType* element_type, int count );
BardArray* BardArray_resized( BardArray* old_array, int new_count, int copy_count );

#endif // BARD_ARRAY_H

