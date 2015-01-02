#include "SuperCStringBuilder.h"

#include <string.h>

SuperCStringBuilder* SuperCStringBuilder_create( SuperC* super_c )
{
  SuperCStringBuilder* buffer = (SuperCStringBuilder*) SuperC_allocate( super_c, sizeof(SuperCStringBuilder) );
  buffer->super_c = super_c;
  return buffer;
}

SuperCStringBuilder* SuperCStringBuilder_destroy( SuperCStringBuilder* buffer )
{
  if ( !buffer ) return 0;

  buffer->characters = SuperC_free( buffer->super_c, buffer->characters );
  buffer->count = 0;
  buffer->capacity = 0;

  return 0;
}

SuperCStringBuilder* SuperCStringBuilder_ensure_additional_capacity( SuperCStringBuilder* buffer, int additional_capacity )
{
  return SuperCStringBuilder_ensure_capacity( buffer, buffer->count + additional_capacity );
}

SuperCStringBuilder* SuperCStringBuilder_ensure_capacity( SuperCStringBuilder* buffer, int minimum_capacity )
{
  if ( !minimum_capacity ) minimum_capacity = 10;
  if (buffer->capacity >= minimum_capacity) return buffer;

  {
    unsigned short* new_characters = SuperC_allocate( buffer->super_c, minimum_capacity << 1 );

    if (buffer->characters)
    {
      memcpy( new_characters, buffer->characters, (buffer->count<<1) );
    }

    buffer->characters = new_characters;
    buffer->capacity = minimum_capacity;
  }

  return buffer;
}

SuperCStringBuilder* SuperCStringBuilder_print_character( SuperCStringBuilder* buffer, int ch )
{
  int count = buffer->count;

  if (count >= buffer->capacity) SuperCStringBuilder_ensure_capacity( buffer, count << 1 );

  buffer->characters[ buffer->count++ ] = (short int) ch;

  return buffer;
}

unsigned short* SuperCStringBuilder_to_string( SuperCStringBuilder* buffer )
{
  int count = buffer->count;
  unsigned short* st = SuperC_allocate( buffer->super_c, (count+1)<<1 );
  st[count] = 0;

  if (count)
  {
    unsigned short* dest = st - 1;
    unsigned short* src = buffer->characters - 1;

    while (--count >= 0)
    {
      *(++dest) = *(++src);
    }
  }

  return st;
}

char* SuperCStringBuilder_to_c_string( SuperCStringBuilder* buffer )
{
  int count = buffer->count;
  char* st = SuperC_allocate( buffer->super_c, count+1 );
  st[count] = 0;

  if (count)
  {
    char* dest = st - 1;
    unsigned short* src = buffer->characters - 1;

    while (--count >= 0)
    {
      *(++dest) = (char) *(++src);
    }
  }

  return st;
}

