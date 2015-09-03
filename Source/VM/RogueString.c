//=============================================================================
//  RogueString.c
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#include "Rogue.h"
#include <string.h>

RogueType* RogueTypeString_create( RogueVM* vm )
{
  RogueType* THIS = RogueType_create( vm, "String", sizeof(RogueString) );
  THIS->print = RoguePrintFn_string;
  return THIS;
}

void RoguePrintFn_string( void* object, RogueStringBuilder* builder )
{

  RogueStringBuilder_print_characters( builder, ((RogueString*)object)->characters, ((RogueString*)object)->count );
}

RogueString* RogueString_create( RogueVM* vm, RogueInteger count )
{
  int size = sizeof(RogueString) + count * sizeof(RogueCharacter);
  RogueString* THIS = (RogueString*) RogueType_create_object( vm->type_String, size );
  THIS->count = count;
  return THIS;
}

RogueString* RogueString_create_from_characters( RogueVM* vm, RogueCharacter* characters, RogueInteger count )
{
  RogueString* THIS = RogueString_create( vm, count );
  memcpy( THIS->characters, characters, count*sizeof(RogueCharacter) );
  return RogueString_update_hash_code( THIS );
}

RogueString* RogueString_create_from_utf8( RogueVM* vm, const char* utf8, int utf8_count )
{
  int decoded_count;
  RogueString* THIS;

  if (utf8_count == -1) utf8_count = strlen( utf8 );
  decoded_count = RogueUTF8_decoded_count( utf8, utf8_count );
  THIS = RogueString_create( vm, decoded_count );
  RogueUTF8_decode( utf8, THIS->characters, decoded_count );
  return RogueString_update_hash_code( THIS );
}

void RogueString_log( RogueString* THIS )
{
  if ( !THIS )
  {
    printf( "null" );
  }
  else
  {
    int i;
    for (i=0; i<THIS->count; ++i)
    {
      RogueCharacter ch = THIS->characters[i];
      putc( (char) ch, stdout );
    }
  }
}

RogueString* RogueString_update_hash_code( RogueString* THIS )
{
  RogueCharacter* src = THIS->characters - 1;
  int hash_code = 0;
  int count = THIS->count;
  while (--count >= 0)
  {
    hash_code = ((hash_code << 3) - hash_code) + *(++src);
  }
  THIS->hash_code = hash_code;
  return THIS;
}

