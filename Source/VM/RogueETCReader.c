//=============================================================================
//  RogueETCReader.c
//
//  2015.09.03 by Abe Pralle
//=============================================================================
#include "Rogue.h"

//-----------------------------------------------------------------------------
//  ETCReader Type
//-----------------------------------------------------------------------------
RogueType* RogueTypeETCReader_create( RogueVM* vm )
{
  RogueType* THIS = RogueVM_create_type( vm, 0, "ETCReader", sizeof(RogueETCReader) );
  return THIS;
}

//-----------------------------------------------------------------------------
//  ETCReader Object
//-----------------------------------------------------------------------------
RogueETCReader* RogueETCReader_create( RogueVM* vm, RogueString* filepath )
{
  RogueETCReader* reader = RogueObject_create( vm->type_ETCReader, -1 );
  reader->filepath = RogueVM_consolidate_string( vm, filepath );

  {
    RogueInteger size;
    FILE* fp = fopen( RogueString_to_c_string(filepath), "rb" );
    if (fp)
    {
      RogueArray* file_bytes;
      RogueInteger decoded_count;

      fseek( fp, 0, SEEK_END );
      size = (RogueInteger) ftell( fp );
      fseek( fp, 0, SEEK_SET );

      file_bytes = RogueByteArray_create( vm, size );
      fread( file_bytes->bytes, 1, size, fp );
      fclose( fp );

      decoded_count = RogueUTF8_decoded_count( (char*) file_bytes->bytes, size );
      reader->data = RogueCharacterArray_create( vm, decoded_count );
      RogueUTF8_decode( (char*) file_bytes->bytes, size, reader->data->characters );

      reader->count = size;
    }
  }

  return reader;
}

int RogueETCReader_consume( RogueETCReader* THIS, RogueCharacter ch )
{
  RogueInteger index = THIS->position.index;
  if (index == THIS->count) return 0;
  if (ch != THIS->data->characters[index]) return 0;
  ++THIS->position.index;
  return 1;
}

int RogueETCReader_consume_whitespace( RogueETCReader* THIS )
{
  int consumed_any = 0;
  int count = THIS->count;
  int i = THIS->position.index;
  while (i < count)
  {
    RogueCharacter ch = THIS->data->characters[i];
    if (ch <= 32 || ch == 127)
    {
      if (ch == '\n') return consumed_any;
      consumed_any = 1;
    }
    else
    {
      return consumed_any;
    }
    i = ++THIS->position.index;
  }
  return consumed_any;
}

int RogueETCReader_has_another( RogueETCReader* THIS )
{
  return (THIS->position.index < THIS->count);
}

RogueLogical RogueETCReader_next_is_digit( RogueETCReader* THIS, RogueInteger base )
{
  RogueCharacter ch;
  RogueInteger index = THIS->position.index;

  if (index >= THIS->count) return 0;

  ch = THIS->data->characters[ index ];
  if (ch >= '0' && ch <= '9') return 1;
  if (ch >= 'a' && ch <= 'z' && ((ch-'a')+10) < base) return 1;
  if (ch >= 'A' && ch <= 'Z' && ((ch-'A')+10) < base) return 1;
  return 0;
}

RogueCharacter RogueETCReader_peek( RogueETCReader* THIS, RogueInteger lookahead )
{
  RogueInteger index = THIS->position.index + lookahead;
  if (index >= THIS->count) return 0;
  return THIS->data->characters[ index ];
}

RogueCharacter RogueETCReader_read( RogueETCReader* THIS )
{
  RogueCharacter result = THIS->data->characters[ THIS->position.index++ ];
  switch (result)
  {
    case '\t':
      THIS->position.column += 2;
      return result;

    case '\n':
      THIS->position.column = 1;
      ++THIS->position.line;
      return result;

    default:
      ++THIS->position.column;
      return result;
  }
}

RogueInteger RogueETCReader_read_digit( RogueETCReader* THIS )
{
  RogueInteger digit = RogueETCReader_read( THIS );

  if (digit >= '0' && digit <= '9') digit -= '0';
  else if (digit >= 'a' && digit <= 'z') digit = 10 + (digit - 'a');
  else if (digit >= 'A' && digit <= 'Z') digit = 10 + (digit - 'A');

  return digit;
}

