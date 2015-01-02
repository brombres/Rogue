#include "Bard.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


//=============================================================================
// BardVMMessageWriter
//=============================================================================
BardVMMessageWriter* BardVMMessageWriter_create()
{
  int initial_capacity = 1024;
  BardVMMessageWriter* writer = (BardVMMessageWriter*) CROM_SYSTEM_MALLOC( sizeof(BardVMMessageWriter) );
  memset( writer, 0, sizeof(BardVMMessageWriter) );
  writer->data = (BardByte*) CROM_SYSTEM_MALLOC( initial_capacity * sizeof(BardByte) );
  writer->capacity = initial_capacity;
  BardVMMessageWriter_clear( writer );
  return writer;
}

BardVMMessageWriter* BardVMMessageWriter_destroy( BardVMMessageWriter* writer )
{
  if (writer)
  {
    BardVMMessageWriter_clear( writer );
    if (writer->data)
    {
      CROM_SYSTEM_FREE( writer->data );
      writer->data = 0;
    }
    CROM_SYSTEM_FREE( writer );
  }
  return NULL;
}

void BardVMMessageWriter_begin_message( BardVMMessageWriter* writer, const char* message_name, 
    int message_id, const char* origin_name )
{
  int   len = (int) strlen(message_name);
  int   origin_len = (int) strlen( origin_name );
  int   count, capacity;
  char* data;

  if (writer->closed) BardVMMessageWriter_clear( writer );
  BardVMMessageWriter_ensure_additional_capacity( writer, len+origin_len+70 );

  count = writer->count;
  capacity = writer->capacity;
  data = (char*) writer->data;

  if (writer->argument_count)
  {
    data[ count ] = '}';
    data[ count+1 ] = ',';
    count += 2;
    writer->argument_count = 0;
  }
  else 
  {
    if (count > 1) data[ count++ ] = ',';
  }

  snprintf( data+count, capacity - count, "{\"m_type\":\"%s\",\"m_id\":%d,\"m_origin\":\"%s\"", message_name, message_id, origin_name );
  while (data[++count]) { }

  writer->count = count;
  ++writer->argument_count;
}

void BardVMMessageWriter_clear( BardVMMessageWriter* writer )
{
  writer->count = 0;
  writer->argument_count = 0;

  writer->data[ writer->count++ ] = '[';
  writer->closed = 0;
}

void BardVMMessageWriter_ensure_additional_capacity( BardVMMessageWriter* writer, int additional_capacity )
{
  int desired_capacity = writer->count + additional_capacity;
  if (desired_capacity > writer->capacity)
  {
    int double_capacity = writer->capacity * 2;
    if (double_capacity > desired_capacity) desired_capacity = double_capacity;

    {
      BardByte* new_data = (BardByte*) CROM_SYSTEM_MALLOC( desired_capacity * sizeof(BardByte) );
      memcpy( new_data, writer->data, writer->count * sizeof(BardByte) );

      CROM_SYSTEM_FREE( writer->data );
      writer->data = new_data;
      writer->capacity = desired_capacity;
    }
  }
}

void BardVMMessageWriter_write_arg( BardVMMessageWriter* writer, const char* arg_name, const char* format, va_list args )
{
  int   count = writer->count;
  int   name_len = (int) strlen(arg_name);
  int   format_len = (int) strlen(format);
  int   capacity;
  char* data;

  ++writer->argument_count;

  BardVMMessageWriter_ensure_additional_capacity( writer, name_len+format_len+80 );

  capacity = writer->capacity;
  data = (char*) writer->data;

  snprintf( data+count, capacity-count, ",\"%s\":", arg_name );
  count += name_len + 4;

  vsnprintf( data+count, capacity-count, format, args );
  while (data[++count]) { }  // find the new end

  writer->count = count;
}

BardByte* BardVMMessageWriter_to_c_string( BardVMMessageWriter* writer )
{
  if ( !writer->closed )
  {
    BardVMMessageWriter_ensure_additional_capacity( writer, 3 );

    // End current message
    if (writer->argument_count > 0) 
    {
      writer->data[writer->count++] = '}';
      writer->argument_count = 0;
    }

    // End message list
    writer->data[writer->count++] = ']';
    writer->closed = 1;

    // Null terminate
    writer->data[writer->count] = 0;
  }

  return writer->data;
}


//=============================================================================
// BardVMMessageReader
//=============================================================================
BardVMMessageReader* BardVMMessageReader_create()
{
  int initial_capacity = 1024;

  BardVMMessageReader* reader = (BardVMMessageReader*) CROM_SYSTEM_MALLOC( sizeof(BardVMMessageReader) );
  memset( reader, 0, sizeof(BardVMMessageReader) );

  reader->data = (BardByte*) CROM_SYSTEM_MALLOC( initial_capacity * sizeof(BardByte) );
  reader->capacity = initial_capacity;
  return reader;
}

BardVMMessageReader* BardVMMessageReader_destroy( BardVMMessageReader* reader )
{
  if (reader)
  {
    if (reader->data)
    {
      CROM_SYSTEM_FREE( reader->data );
      reader->data = 0;
    }
    CROM_SYSTEM_FREE( reader );
  }
  return NULL;
}

void BardVMMessageReader_advance( BardVMMessageReader* reader )
{
  int i1 = reader->message_start;
  if (i1 == -1) return;

  i1 = BardVMMessageReader_find_end_of_element( reader, i1 );
  if (i1 >= 0)
  {
    i1 = BardVMMessageReader_consume_symbol( reader, i1+1, ',' );
    if (i1 >= 0 && reader->data[i1] != '{') i1 = -1;
  }
  reader->message_start = i1;
}

int BardVMMessageReader_consume_symbol( BardVMMessageReader* reader, int i1, int symbol )
{
  i1 = BardVMMessageReader_consume_whitespace( reader, i1 );
  if (i1 == -1) return -1;

  if (reader->data[i1] != symbol) return -1;

  return BardVMMessageReader_consume_whitespace( reader, i1+1 );
}

int BardVMMessageReader_consume_whitespace( BardVMMessageReader* reader, int i1 )
{
  int ch;

  BardByte* data = reader->data;
  if (i1 == -1) return -1;

  ch = data[i1];
  while (ch <= 32 || ch == 127)
  {
    if ( !ch ) return -1;
    ch = data[++i1];
  }

  return i1;
}

void BardVMMessageReader_ensure_capacity( BardVMMessageReader* reader, int desired_capacity )
{
  if (desired_capacity > reader->capacity)
  {
    int double_capacity = reader->capacity * 2;
    if (double_capacity > desired_capacity) desired_capacity = double_capacity;

    CROM_SYSTEM_FREE( reader->data );
    reader->data = (BardByte*) CROM_SYSTEM_MALLOC( desired_capacity * sizeof(BardByte) );

    reader->capacity = desired_capacity;
  }
}

int BardVMMessageReader_find_key( BardVMMessageReader* reader, const char* key )
{
  BardByte* data = reader->data;
  int key_count = (int) strlen(key);
  int i1 = reader->message_start;
  if (i1 == -1) return 0;

  // Position i1 at first key
  i1 = BardVMMessageReader_consume_whitespace( reader, reader->message_start+1 );
  while (i1 >= 0 && data[i1] == '"')
  {
    int found_key;
    int i2 = BardVMMessageReader_find_end_of_element( reader, i1 );
    if (i2 == -1) return 0;

    found_key = (0 == strncmp((char*)data+i1+1,key,key_count));

    // Set cursor to data value regardless
    i1 = BardVMMessageReader_consume_symbol( reader, i2+1, ':' );
    if (i1 == -1) return 0;

    i2 = BardVMMessageReader_find_end_of_element( reader, i1 );
    if (i2 == -1) return 0;

    if (found_key)
    {
      //printf( "Found key %s with value at %d..%d\n", key, i1, i2 );
      reader->element_i1 = i1;
      reader->element_i2 = i2;
      return 1;
    }
    else
    {
      i1 = BardVMMessageReader_consume_symbol( reader, i2+1, ',' );
      if (i1 == -1) return 0;
    }
  }

  return 0;
}


int BardVMMessageReader_find_end_of_element( BardVMMessageReader* reader, int i )
{
  BardByte* data = reader->data;
  int count = reader->count;
  int ch;

  if (i == -1) return -1;

  ch = data[i];
  if (ch == '"')
  {
    // String
    ch = data[++i];
    while (ch && ch != '"')
    {
      if (ch == '\\')
      {
        if (data[++i] == 'u')
        {
          i += 4;
          if (i >= count) return -1;
        }
      }
      ch = data[++i];
    }
    if (ch == '"') return i;
  }
  else if (ch == '{')
  {
    // Nested Table
    i = BardVMMessageReader_consume_whitespace( reader, i+1 );
    if (i == -1) return -1;
    if (data[i] == '}') return i;

    while (data[i] == '"')
    {
      // find end of key
      i = BardVMMessageReader_find_end_of_element( reader, i );
      if (i == -1) return -1;

      // read :
      i = BardVMMessageReader_consume_symbol( reader, i+1, ':' );
      if (i == -1) return -1;

      // find end of value
      i = BardVMMessageReader_find_end_of_element( reader, i );
      if (i == -1) return -1;

      i = BardVMMessageReader_consume_whitespace( reader, i+1 );
      if (i == -1)        return -1;
      if (data[i] == '}') return i; 

      // read ','
      i = BardVMMessageReader_consume_symbol( reader, i, ',' );
      if (i == -1) return -1;
    }
    return -1;
  }
  else if (ch == '[')
  {
    // Nested List
    i = BardVMMessageReader_consume_whitespace( reader, i+1 );
    if (i == -1) return -1;
    if (data[i] == ']') return i;

    for (;;)
    {
      // find end of key
      i = BardVMMessageReader_find_end_of_element( reader, i );
      if (i == -1) return -1;

      i = BardVMMessageReader_consume_whitespace( reader, i+1 );
      if (i == -1)        return -1;
      if (data[i] == ']') return  i; 

      // read ','
      i = BardVMMessageReader_consume_symbol( reader, i, ',' );
      if (i == -1) return -1;
    }
  }
  else if ((ch >= '0' && ch <= '9') || ch == '-')
  {
    // Number
    int peek;
    if (ch == '-') ch = data[++i];

    if (ch < '0' || ch > '9') return -1;

    if (ch >= '1')
    {
      peek = data[i+1];
      while (peek >= '0' && peek <= '9')
      {
        ++i;
        peek = data[i+1];
      }
    }

    if (data[i+1] == '.')
    {
      ++i;
      ch = data[++i];
      if (ch < '0' || ch > '9') return -1;

      peek = data[i+1];
      while (peek >= '0' && peek <= '9')
      {
        ++i;
        peek = data[i+1];
      }
    }

    peek = data[ i+1 ];
    if (peek == 'e' || peek == 'E')
    {
      ++i;
      peek = data[i+1];
      if (peek == '-' || peek == '+') ++i;

      peek = data[i+1];
      if (peek < '0' || peek > '9') return -1;

      ++i;
      peek = data[i+1];
      while (peek >= '0' && peek <= '9')
      {
        ++i;
        peek = data[i+1];
      }
    }

    return i;
  }
  else if (ch == 'f')
  {
    // false
    if (i+4 >= count) return -1;
    if (data[i+1] != 'a' || data[i+2] != 'l' || data[i+3] != 's' || data[i+4] != 'e') return -1;
    return i + 4;
  }
  else if (ch == 'n')
  {
    // null
    if (i+3 >= count) return -1;
    if (data[i+1] != 'u' || data[i+2] != 'l' || data[i+3] != 'l') return -1;
    return i + 3;
  }
  else if (ch == 't')
  {
    // true
    if (i+3 >= count) return -1;
    if (data[i+1] != 'r' || data[i+2] != 'u' || data[i+3] != 'e') return -1;
    return i + 3;
  }
  return -1;
}

int  BardVMMessageReader_has_another( BardVMMessageReader* reader )
{
  // Reader is always kept positioned on the opening '{' of the next message so
  // a valid message_start index means a valid message.
  return (reader->message_start >= 0);
}

char* BardVMMessageReader_read_c_string( BardVMMessageReader* reader, const char* key, char* buffer, int buffer_size )
{
  // If buffer is NULL then a new string is created which is the caller's responsibility to free.
  // If buffer is != NULL then at most buffer_size-1 characters will be written to it followed by
  // a null terminator.
  if (BardVMMessageReader_find_key(reader,key))
  {
    int i1 = reader->element_i1;
    int i2 = reader->element_i2;
    int string_size;
    int is_literal_string = 0;

    if (reader->data[i1] == '"')
    {
      int i;
      BardByte* data = reader->data;

      is_literal_string = 1;

      // Omit bounding quotes from result.
      ++i1;
      --i2;

      // Decode the characters to count up the length of the string
      string_size = 0;
      for (i=i1; i<=i2; ++i)
      {
        int ch = data[i];
        if (ch == '\\')
        {
          if (data[++i] == 'u') i += 4;
        }
        ++string_size;
      }
    }
    else
    {
      string_size = (i2 - i1) + 1;
    }

    if ( !buffer )
    {
      buffer_size = string_size + 1;
      buffer = (char*) CROM_SYSTEM_MALLOC( buffer_size );
    }
    else if (string_size + 1 > buffer_size)
    {
      string_size = buffer_size - 1;
    }

    if (is_literal_string)
    {
      // Decode copy
      int count = string_size;
      BardByte* dest = (BardByte*) (buffer - 1);
      BardByte* src  = (reader->data + i1) - 1;

      // Decode the characters to count up the length of the string
      while (--count >= 0)
      {
        int ch = *(++src);
        if (ch == '\\')
        {
          ch = *(++src);
          switch (ch)
          {
            case 'b':  ch = '\b'; break;
            case 'f':  ch = '\f'; break;
            case 'n':  ch = '\n'; break;
            case 'r':  ch = '\r'; break;
            case 't':  ch = '\t'; break;
            case 'u':
              {
                ch = BardUtil_hex_character_to_value( *(++src) );
                ch = (ch << 8) | BardUtil_hex_character_to_value( *(++src) );
                ch = (ch << 8) | BardUtil_hex_character_to_value( *(++src) );
                ch = (ch << 8) | BardUtil_hex_character_to_value( *(++src) );
              }
              break;
          }
        }
        *(++dest) = (BardByte) ch;
      }
    }
    else
    {
      // Straight copy
      memcpy( buffer, reader->data + i1, string_size );
    }
    buffer[string_size] = 0;
    return buffer;
  }

  if (buffer)
  {
    buffer[0] = 0;
    return buffer;
  }
  else
  {
    return "";
  }
}

int BardVMMessageReader_is_null( BardVMMessageReader* reader, const char* key )
{
  if ( !BardVMMessageReader_find_key(reader,key) ) return 0;
  return (reader->data[reader->element_i1] == 'n');
}

BardInteger BardVMMessageReader_read_integer( BardVMMessageReader* reader, const char* key )
{
  return (BardInteger) BardVMMessageReader_read_real( reader, key );
}

BardLogical BardVMMessageReader_read_logical( BardVMMessageReader* reader, const char* key )
{
  return BardVMMessageReader_read_real( reader, key ) ? 1 : 0;
}

BardReal BardVMMessageReader_read_real( BardVMMessageReader* reader, const char* key )
{
  int i1, i2;
  double result = 0.0;

  if ( !BardVMMessageReader_find_key(reader,key) ) return 0.0;

  i1 = reader->element_i1;
  i2 = reader->element_i2;

  switch (reader->data[i1])
  {
    case '"': // Omit bounding quotes from result.
      ++i1;
      --i2;
      break;

    case 't': // true
      return 1.0;

    case 'f': // false
      return 0.0;

    case 'n': // null
      return 0.0;
  }

  sscanf( (char*) (reader->data + i1), "%lf", &result );

  return result;
}

void BardVMMessageReader_set_c_string_data( BardVMMessageReader* reader, const char* data, int data_count )
{
  int i;

  BardVMMessageReader_ensure_capacity( reader, data_count + 1 );
  reader->count = data_count;
  strcpy( (char*) reader->data, data );

  reader->element_i1 = 0;
  reader->element_i2 = 0;
  reader->message_start = -1;

  i = BardVMMessageReader_consume_whitespace( reader, 0 );
  if (i >= 0 && reader->data[i] == '[')
  {
    i = BardVMMessageReader_consume_whitespace( reader, i+1 );
    if (i >= 0 && reader->data[i] == '{') reader->message_start = i;
  }
}

void BardVMMessageReader_set_character_data( BardVMMessageReader* reader, BardCharacter* data, int data_count )
{
  BardByte* dest;
  int i;

  BardVMMessageReader_ensure_capacity( reader, data_count + 1 );
  dest = reader->data - 1;
  --data;

  reader->count = data_count;
  while (--data_count >= 0) *(++dest) = (BardByte) *(++data);
  *(++dest) = 0;

  reader->element_i1 = 0;
  reader->element_i2 = 0;
  reader->message_start = -1;

  i = BardVMMessageReader_consume_whitespace( reader, 0 );
  if (i >= 0 && reader->data[i] == '[')
  {
    i = BardVMMessageReader_consume_whitespace( reader, i+1 );
    if (i >= 0 && reader->data[i] == '{') reader->message_start = i;
  }
}

//=============================================================================
// BardMessageListener
//=============================================================================
BardMessageListener* BardMessageListener_create( const char* name, BardMessageListenerCallback fn, int ordering, void* user_data )
{
  BardMessageListener* listener = (BardMessageListener*) CROM_SYSTEM_MALLOC( sizeof(BardMessageListener) );
  listener->name = BardVMCString_duplicate( name );
  listener->fn = fn;
  listener->next_listener = NULL;
  listener->ordering = ordering;
  listener->user_data = user_data;
  return listener;
}

BardMessageListener* BardMessageListener_destroy( BardMessageListener* listener )
{
  if (listener)
  {
    CROM_SYSTEM_FREE( listener->name );
    CROM_SYSTEM_FREE( listener );
  }
  return NULL;
}

