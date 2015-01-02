#ifndef BARD_BC_READER
#define BARD_BC_READER

#include "Bard.h"

typedef struct BardFileReader
{
  unsigned char* data;
  int            count;
  int            position;
  int            free_data_on_close;
} BardFileReader;

BardFileReader* BardFileReader_create_with_filename( const char* filename );
BardFileReader* BardFileReader_create_with_data( const char* filename, char* data, int count, int free_data_on_close );
void          BardFileReader_destroy( BardFileReader* reader );

BardFileReader* BardFileReader_init_with_filename( BardFileReader* reader, const char* filename );
BardFileReader* BardFileReader_init_with_data( BardFileReader* reader, const char* filename, char* data, int count, int free_data_on_close );

BardFileReader* BardFileReader_close( BardFileReader* reader );

int         BardFileReader_read_byte( BardFileReader* reader );
int         BardFileReader_read_integer( BardFileReader* reader );
int         BardFileReader_read_integer( BardFileReader* reader );
int         BardFileReader_consume( BardFileReader* reader, const char* text );
char*       BardFileReader_read_id( BardFileReader* reader );

#endif // BARD_BC_READER
