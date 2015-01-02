#ifndef BARD_STRING_H
#define BARD_STRING_H

typedef struct BardString
{
  struct BardObject header;

  // ---- BardString Parts ----
  int           count;
  int           hash_code;
  BardCharacter characters[1];

} BardString;

BardString* BardString_create_with_size( struct BardVM* vm, int size );
BardString* BardString_create_with_c_string( struct BardVM* vm, const char* st );
BardString* BardString_create_with_c_string_and_length( struct BardVM* vm, const char* st, int len );

BardString* BardString_set_hash_code( BardString* obj );
char*       BardString_to_c_string( BardString* st, char* destination_buffer, int destination_buffer_capacity );
void        BardString_print( BardString* st );

//BardObject* BardString_write_string( BardObject* st, BardObject* other );

#endif // BARD_STRING_H
