#ifndef BARD_MESSAGE_MANAGER_H
#define BARD_MESSAGE_MANAGER_H

#include <stdarg.h>

//=============================================================================
// BardVMMessageWriter
//=============================================================================
typedef struct BardVMMessageWriter
{
  BardByte* data;
  int       capacity;
  int       count;
  int       argument_count;
  int       closed;
} BardVMMessageWriter;

BardVMMessageWriter* BardVMMessageWriter_create();
BardVMMessageWriter* BardVMMessageWriter_destroy( BardVMMessageWriter* writer );

void      BardVMMessageWriter_begin_message( BardVMMessageWriter* writer, const char* message_name, 
                                             int message_id, const char* origin_name );
void      BardVMMessageWriter_clear( BardVMMessageWriter* writer );
void      BardVMMessageWriter_ensure_additional_capacity( BardVMMessageWriter* writer, int additional_capacity );
BardByte* BardVMMessageWriter_to_c_string( BardVMMessageWriter* writer );
void      BardVMMessageWriter_write_arg( BardVMMessageWriter* writer, const char* arg_name, const char* format, 
                                     va_list args );


//=============================================================================
// BardVMMessageReader
//=============================================================================
typedef struct BardVMMessageReader
{
  BardByte* data;
  int       capacity;
  int       count;
  int       message_start;
  int       element_i1;  // of searched-for key
  int       element_i2;
} BardVMMessageReader;

BardVMMessageReader* BardVMMessageReader_create();
BardVMMessageReader* BardVMMessageReader_destroy( BardVMMessageReader* reader );

void  BardVMMessageReader_advance( BardVMMessageReader* reader );
int   BardVMMessageReader_consume_symbol( BardVMMessageReader* reader, int i1, int symbol );
int   BardVMMessageReader_consume_whitespace( BardVMMessageReader* reader, int i1 );
void  BardVMMessageReader_ensure_capacity( BardVMMessageReader* reader, int desired_capacity );
int   BardVMMessageReader_find_key( BardVMMessageReader* reader, const char* key );
int   BardVMMessageReader_find_end_of_element( BardVMMessageReader* reader, int i1 );
int   BardVMMessageReader_has_another( BardVMMessageReader* reader );
int         BardVMMessageReader_is_null( BardVMMessageReader* reader, const char* key );
char*       BardVMMessageReader_read_c_string( BardVMMessageReader* reader, const char* key, char* buffer, int buffer_size );
BardInteger BardVMMessageReader_read_integer( BardVMMessageReader* reader, const char* key );
BardLogical BardVMMessageReader_read_logical( BardVMMessageReader* reader, const char* key );
BardReal    BardVMMessageReader_read_real( BardVMMessageReader* reader, const char* key );
void  BardVMMessageReader_set_c_string_data( BardVMMessageReader* reader, const char* data, int data_count );
void  BardVMMessageReader_set_character_data( BardVMMessageReader* reader, BardCharacter* data, int data_count );



//=============================================================================
// BardMessageListener
//=============================================================================
typedef struct BardMessageListener
{
  char*                       name;
  BardMessageListenerCallback fn;
  struct BardMessageListener* next_listener;
  int                         ordering;
  void*                       user_data;
} BardMessageListener;

BardMessageListener* BardMessageListener_create( const char* name, BardMessageListenerCallback fn, int ordering, void* user_data );
BardMessageListener* BardMessageListener_destroy( BardMessageListener* info );

#endif // BARD_MESSAGE_MANAGER_H

