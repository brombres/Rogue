//=============================================================================
//  RogueStringBuilder.c
//
//  2015.08.31 by Abe Pralle
//=============================================================================
#include "Rogue.h"

void RogueStringBuilder_init( RogueStringBuilder* THIS, RogueVM* vm, RogueInteger initial_capacity )
{
  THIS->vm = vm;
  THIS->count = 0;
  if (initial_capacity <= ROGUE_STRING_BUILDER_INTERNAL_BUFFER_SIZE)
  {
    THIS->capacity = ROGUE_STRING_BUILDER_INTERNAL_BUFFER_SIZE;
    THIS->characters = THIS->internal_buffer;
  }
  else
  {
    THIS->capacity = initial_capacity;
    THIS->external_buffer = RogueVMArray_create( vm, sizeof(RogueCharacter), initial_capacity );
    THIS->characters = THIS->external_buffer->characters;
  }
}

void RogueStringBuilder_log( RogueStringBuilder* THIS )
{
  int n = THIS->count;
  for (int i=0; i<n; ++i)
  {
    int ch = THIS->characters[i];
    if (ch <= 0x7f)
    {
      //0..0x7f (0-127) is written normally
      putc( (char)ch, stdout );
    }
    else if (ch <= 0x7ff)
    {
      //0x80..0x7ff is encoded in two bytes.
      //0000 0xxx yyyy yyyy -> 110xxxyy 10yyyyyy

      //top_5    = (1100 0000 | ((ch>>6) & 1 1111))
      int top_5    = (0xc0      | ((ch>>6) & 0x1f));

      //bottom_6 = (1000 0000 | (ch & 11 1111))
      int bottom_6 = (0x80      | (ch & 0x3f));

      putc( (char)top_5, stdout );
      putc( (char)bottom_6, stdout );
    }
    else
    {
      //0x8000..0xffff is encoded in three bytes.
      //xxxx xxxx yyyy yyyy -> 1110xxxx 10xxxxyy 10yyyyyy

      //top_4    = (1110 0000 | ((ch>>12) & 1111))
      int top_4    = (0xe0      | ((ch>>12) & 0xf));

      //mid_6    = (1000 0000 | ((ch>>6)  & 11 1111))
      int mid_6    = (0x80      | ((ch>>6)  & 0x3f));

      //bottom_6 = (1000 0000 | (ch & 11 1111))
      int bottom_6 = (0x80 | (ch & 0x3f));

      putc( (char)top_4, stdout );
      putc( (char)mid_6, stdout );
      putc( (char)bottom_6, stdout );
    }
  }
}

void RogueStringBuilder_print_character( RogueStringBuilder* THIS, RogueCharacter value )
{
  if (THIS->count == THIS->capacity) RogueStringBuilder_reserve( THIS, 1 );
  THIS->characters[ THIS->count++ ] = value;
}

void RogueStringBuilder_print_characters( RogueStringBuilder* THIS, RogueCharacter* values, RogueInteger len )
{
  RogueStringBuilder_reserve( THIS, len );
  memcpy( THIS->characters+THIS->count, values, len*sizeof(RogueCharacter) );
  THIS->count += len;
}

void RogueStringBuilder_print_integer( RogueStringBuilder* THIS, RogueInteger value )
{
  char st[20];
  sprintf( st, "%d", value );
  RogueStringBuilder_print_c_string( THIS, st );
}

void RogueStringBuilder_print_long( RogueStringBuilder* THIS, RogueLong value )
{
  char st[30];
  sprintf( st, "%lld", value );
  RogueStringBuilder_print_c_string( THIS, st );
}

void RogueStringBuilder_print_object( RogueStringBuilder* THIS, void* value )
{
  if (value)
  {
    RogueObject_to_string( value, THIS );
  }
  else
  {
    RogueStringBuilder_print_c_string( THIS, "null" );
  }
}

void RogueStringBuilder_print_real( RogueStringBuilder* THIS, RogueReal value )
{
  char st[40];
  sprintf( st, "%.4lf", value );
  RogueStringBuilder_print_c_string( THIS, st );
}

void RogueStringBuilder_print_c_string( RogueStringBuilder* THIS, const char* value )
{
  RogueStringBuilder_print_c_string_with_count( THIS, value, -1 );
}

void RogueStringBuilder_print_c_string_with_count( RogueStringBuilder* THIS, const char* value, RogueInteger count )
{
  int decoded_count;

  if (count <= -1) count = strlen( value );
  decoded_count = RogueUTF8_decoded_count( value, count );

  RogueStringBuilder_reserve( THIS, decoded_count );
  RogueUTF8_decode( value, count, THIS->characters+THIS->count );
  THIS->count += decoded_count;
}

void RogueStringBuilder_print_string_builder( RogueStringBuilder* THIS, RogueStringBuilder* other )
{
  RogueStringBuilder_print_characters( THIS, other->characters, other->count );
}

void RogueStringBuilder_reserve( RogueStringBuilder* THIS, RogueInteger additional )
{
  RogueInteger required = THIS->count + additional;
  if (THIS->capacity >= required) return;

  RogueInteger doubled = THIS->capacity << 1;
  if (doubled > required) required = doubled;

  THIS->external_buffer = RogueVMArray_create( THIS->vm, sizeof(RogueCharacter), required );
  RogueCharacter* new_characters = THIS->external_buffer->characters;
  memcpy( new_characters, THIS->characters, THIS->count*sizeof(RogueCharacter) );

  THIS->characters = new_characters;
  THIS->capacity = required;
}

RogueString* RogueStringBuilder_to_string( RogueStringBuilder* THIS )
{
  return RogueString_create_from_characters( THIS->vm, THIS->characters, THIS->count );
}

