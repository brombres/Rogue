#include <stdio.h>
#include <string.h>

#include "BardXC.h"

//=============================================================================
//  BardString
//=============================================================================
BardString* BardString_create_with_size( BardVM* vm, int size )
{
  int total_size = sizeof(BardString) + sizeof(BardXCCharacter) * size;
  BardString* obj = (BardString*) BardMM_create_object( &vm->mm, vm->type_String, total_size );
  obj->count = size;
  return obj;
}

BardString* BardString_create_with_c_string( BardVM* vm, const char* st )
{
  return BardString_create_with_c_string_and_length( vm, st, -1 );
}

BardString* BardString_create_with_c_string_and_length( BardVM* vm, const char* st, int size )
{
  BardString* obj;

  if (size == -1) size = (int) strlen(st);
  obj = BardString_create_with_size( vm, size );

  {
    const char*    src = (st - 1);
    BardXCCharacter* dest = obj->characters - 1;
    int count = size;
    while (--count >= 0)
    {
      *(++dest) = *(++src);
    }
  }

  return BardString_set_hash_code( obj );
}

BardString* BardString_set_hash_code( BardString* obj )
{
  int hash_code = 0;
  int count = obj->count;
  BardXCCharacter* cur = obj->characters - 1;

  while (--count >= 0)
  {
    // hash_code = hash_code * 7 + ch
    hash_code = ((hash_code << 3) - hash_code) + *(++cur);
  }

  obj->hash_code = hash_code;

  return obj;
}

char* BardString_to_c_string( BardString* st, char* destination_buffer, int destination_buffer_capacity )
{
  BardXCCharacter* src = st->characters;

  int count = st->count;
  if (count >= destination_buffer_capacity) count = destination_buffer_capacity - 1;

  destination_buffer[count] = 0;
  while (--count >= 0)
  {
    destination_buffer[count] = (char) src[count];
  }
  
  return destination_buffer;
}


void BardString_print( BardString* st )
{
  BardXCCharacter* src = st->characters - 1;
  int count = st->count;
  while (--count >= 0)
  {
    putc( (char) *(++src), stdout );
  }
}

