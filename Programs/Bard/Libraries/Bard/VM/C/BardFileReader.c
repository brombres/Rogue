#include "Bard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


BardFileReader* BardFileReader_create_with_filename( const char* filename )
{
  return BardFileReader_init_with_filename( (BardFileReader*) CROM_SYSTEM_MALLOC(sizeof(BardFileReader)), filename );
}

BardFileReader* BardFileReader_create_with_data( const char* filename, char* data, int count, int free_data_on_close )
{
  return BardFileReader_init_with_data( (BardFileReader*) CROM_SYSTEM_MALLOC(sizeof(BardFileReader)), filename, data, count, free_data_on_close );
}

void BardFileReader_destroy( BardFileReader* reader )
{
  CROM_SYSTEM_FREE( BardFileReader_close(reader) );
}


BardFileReader* BardFileReader_init_with_filename( BardFileReader *reader, const char* filename )
{
  FILE* fp = fopen( filename, "rb" );
  if ( !fp ) return NULL;

  fseek( fp, 0, SEEK_END );
  int size = (int) ftell( fp );
  fseek( fp, 0, SEEK_SET );
  if (size <= 0) return NULL;

  char* buffer = CROM_SYSTEM_MALLOC( size );
  fread( buffer, 1, size, fp );

  fclose( fp );

  return BardFileReader_init_with_data( reader, filename, buffer, size, 1 );
}

BardFileReader* BardFileReader_init_with_data( BardFileReader *reader, const char* filename, char* data, int count, int free_data_on_close )
{
  memset( reader, 0, sizeof(BardFileReader) );
  reader->data = (unsigned char*) data;
  reader->count = count;
  reader->free_data_on_close = free_data_on_close;
  return reader;
}

BardFileReader* BardFileReader_close( BardFileReader *reader )
{
  if (reader->free_data_on_close)
  {
    CROM_SYSTEM_FREE( reader->data );
    memset( reader, 0, sizeof(BardFileReader) );  // clear 'data' and 'free_data_on_close'
  }

  return reader;
}

int BardFileReader_read_byte( BardFileReader* reader )
{
  if (reader->position >= reader->count) return -1;
  return reader->data[ reader->position++ ];
}

int BardFileReader_read_integer( BardFileReader* reader )
{
  // 0: 0xxxxxxx - 0..127
  // 1: 1000xxxx - 1 byte follows (12 bits total, unsigned)
  // 2: 1001xxxx - 2 bytes follow (20 bits total, unsigned)
  // 3: 1010xxxx - 3 bytes follow (28 bits total, unsigned)
  // 4: 10110000 - 4 bytes follow (32 bits total, signed)
  // 5: 11xxxxxx - -64..-1
  int b1;
  
  b1 = BardFileReader_read_byte( reader );

  if (b1 < 128)  return b1;          // encoding 0
  if (b1 >= 192) return (b1 - 256);  // encoding 5

  switch (b1 & 0x30)
  {
    case 0:  // encoding 1
      return ((b1 & 15) << 8) | BardFileReader_read_byte(reader);

    case 16:  // encoding 2
      {
        int b2 = BardFileReader_read_byte(reader);
        int b3 = BardFileReader_read_byte(reader);
        return ((b1 & 15) << 16) | (b2 << 8) | b3;
      }

    case 32:  // encoding 3
      {
        int b2 = BardFileReader_read_byte(reader);
        int b3 = BardFileReader_read_byte(reader);
        int b4 = BardFileReader_read_byte(reader);
        return ((b1 & 15) << 24) | (b2 << 16) | (b3 << 8) | b4;
      }

    case 48:  // encoding 4
      {
        int result = BardFileReader_read_byte( reader );
        result = (result << 8) | BardFileReader_read_byte(reader);
        result = (result << 8) | BardFileReader_read_byte(reader);
        result = (result << 8) | BardFileReader_read_byte(reader);
        return (BardInteger) result;
      }

    default:
      return -1;
  }
}

int BardFileReader_consume( BardFileReader *reader, const char* text )
{
  int ch, i;
  int original_position = reader->position;
  for (i=0; (ch=text[i]); ++i)
  {
    if (BardFileReader_read_byte(reader) != ch)
    {
      reader->position = original_position;
      return 0;
    }
  }
  return 1;
}

char* BardFileReader_read_id( BardFileReader* reader )
{
  int count = BardFileReader_read_integer( reader );
  char* buffer = CROM_SYSTEM_MALLOC( count+1 );

  {
    int i = count;
    for (i=0; i<count; ++i)
    {
      buffer[i] = (char) BardFileReader_read_integer( reader );
    }
  }

  buffer[count] = 0;
  return buffer;
}

