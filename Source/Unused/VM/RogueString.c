//=============================================================================
//  RogueString.c
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#include "Rogue.h"
#include <string.h>

RogueInteger RogueString_intrinsic_fn( RogueIntrinsicFnType fn_type,
    RogueObject* context, void* parameter )
{
  switch (fn_type)
  {
    case ROGUE_INTRINSIC_FN_TRACE:
      return 0;

    case ROGUE_INTRINSIC_FN_HASH_CODE:
      return ((RogueString*)context)->hash_code;

    case ROGUE_INTRINSIC_FN_TO_STRING:
      RogueStringBuilder_print_characters( parameter, ((RogueString*)context)->characters, ((RogueString*)context)->count );

    case ROGUE_INTRINSIC_FN_EQUALS_OBJECT:
    {
      RogueString* a;
      RogueString* b;

      if (context == parameter) return 1;

      a = (RogueString*) context;
      b = parameter;
      if (a->object.type != b->object.type) return 0;

      if (a->hash_code != b->hash_code || a->count != b->count) return 0;

      return (0 == memcmp(a->characters,b->characters,a->count*sizeof(RogueCharacter)));
      break;
    }

    case ROGUE_INTRINSIC_FN_EQUALS_C_STRING:
    {
      RogueCharacter* characters;
      RogueString* a = (RogueString*) context;
      const char* b = parameter;

      int b_len = strlen( b );
      char ch;

      if (a->count != b_len) return 0;

      characters = a->characters - 1;
      --b;

      while ( (ch = *(++b)) )
      {
        if (*(++characters) != ch) return 0;
      }

      return 1;
    }

    case ROGUE_INTRINSIC_FN_EQUALS_CHARACTERS:
      {
        RogueString* THIS = (RogueString*) context;
        RogueCharacterInfo* info = parameter;
        if (THIS->hash_code != info->hash_code || THIS->count != info->count) return 0;

        return (0 == memcmp(THIS->characters,info->characters,info->count*sizeof(RogueCharacter)));
      }
      return 0;
  }

  return 0;
}

RogueType* RogueTypeString_create( RogueVM* vm )
{
  RogueType* THIS    = RogueVM_create_type( vm, 0, "String", sizeof(RogueString) );
  THIS->intrinsic_fn = RogueString_intrinsic_fn;
  return THIS;
}

RogueString* RogueString_create( RogueVM* vm, RogueInteger count )
{
  int size = sizeof(RogueString) + count * sizeof(RogueCharacter);
  RogueString* THIS = (RogueString*) RogueObject_create( vm->type_String, size );
  THIS->count = count;
  return THIS;
}

RogueString* RogueString_create_from_characters( RogueVM* vm, RogueCharacter* characters, RogueInteger count )
{
  RogueString* THIS = RogueString_create( vm, count );
  memcpy( THIS->characters, characters, count*sizeof(RogueCharacter) );
  return RogueString_update_hash_code( THIS );
}

RogueString* RogueString_create_from_c_string( RogueVM* vm, const char* utf8 )
{
  int decoded_count;
  RogueString* THIS;

  RogueInteger utf8_count = strlen( utf8 );
  decoded_count = RogueUTF8_decoded_count( utf8, utf8_count );
  THIS = RogueString_create( vm, decoded_count );
  RogueUTF8_decode( utf8, utf8_count, THIS->characters );
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

RogueInteger RogueString_calculate_hash_code( const char* st )
{
  RogueInteger hash_code = 0;
  RogueInteger ch;
  --st;
  while ( (ch = *(++st)) )
  {
    hash_code = ((hash_code << 3) - hash_code) + ch;
  }
  return hash_code;
}

RogueInteger RogueString_calculate_hash_code_for_characters( RogueCharacter* characters, RogueInteger count )
{
  RogueInteger hash_code = 0;
  --characters;
  while (--count >= 0)
  {
    hash_code = ((hash_code << 3) - hash_code) + *(++characters);
  }
  return hash_code;
}

char* RogueString_to_c_string( RogueString* st )
{
  if (st)
  {
    char* c_string;
    RogueVM* vm = st->object.type->vm;
    RogueInteger utf8_count = RogueUTF8_encoded_count( st->characters, st->count );

    RogueList_resize( vm->c_string_buffer, utf8_count + 1 );

    c_string = (char*) vm->c_string_buffer->array->bytes;
    RogueUTF8_encode( st->characters, st->count, c_string );

    c_string[ utf8_count ] = 0;  // null terminate
    return c_string;
  }
  else
  {
    return "null";
  }
}

