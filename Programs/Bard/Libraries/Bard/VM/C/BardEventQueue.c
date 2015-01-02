#include "Bard.h"

#include <stdlib.h>
#include <string.h>

//=============================================================================
// BardEventQueue
//=============================================================================
BardEventQueue* BardEventQueue_create()
{
  BardEventQueue* queue = (BardEventQueue*) CROM_SYSTEM_MALLOC( sizeof(BardEventQueue) );

  queue->character_capacity = 256;
  queue->character_data = (BardCharacter*) CROM_SYSTEM_MALLOC( queue->character_capacity * sizeof(BardCharacter) );
  queue->character_count = 0;

  queue->real_capacity = 64;
  queue->real_data = (BardReal*) CROM_SYSTEM_MALLOC( queue->real_capacity * sizeof(BardReal) );
  queue->real_count = 0;

  return queue;
}

BardEventQueue* BardEventQueue_destroy( BardEventQueue* queue )
{
  if ( !queue ) return NULL;

  CROM_SYSTEM_FREE( queue->character_data );
  CROM_SYSTEM_FREE( queue->real_data );
  CROM_SYSTEM_FREE( queue );

  return NULL;
}


void BardEventQueue_clear( BardEventQueue* queue )
{
  queue->character_count = 0;
  queue->real_count = 0;
}

void BardEventQueue_ensure_additional_character_capacity( BardEventQueue* queue, int additional_character_capacity )
{
  int necessary = queue->character_count + additional_character_capacity;
  if (necessary > queue->character_capacity)
  {
    BardCharacter* new_data;
    int desired = queue->character_capacity * 2;
    if (necessary > desired) desired = necessary;

    new_data = (BardCharacter*) CROM_SYSTEM_MALLOC( desired * sizeof(BardCharacter) );
    memcpy( new_data, queue->character_data, queue->character_count * sizeof(BardCharacter) );
    CROM_SYSTEM_FREE( queue->character_data );
    queue->character_data = new_data;
  }
}

void BardEventQueue_ensure_additional_real_capacity( BardEventQueue* queue, int additional_character_capacity )
{
  int necessary = queue->real_count + additional_character_capacity;
  if (necessary > queue->real_capacity)
  {
    BardReal* new_data;
    int desired = queue->real_capacity * 2;
    if (necessary > desired) desired = necessary;

    new_data = (BardReal*) CROM_SYSTEM_MALLOC( desired * sizeof(BardReal) );
    memcpy( new_data, queue->real_data, queue->real_count * sizeof(BardReal) );
    CROM_SYSTEM_FREE( queue->real_data );
    queue->real_data = new_data;
  }
}

void BardEventQueue_write_integer( BardEventQueue* queue, BardInteger value )
{
  BardEventQueue_ensure_additional_real_capacity( queue, 1 );
  queue->real_data[ queue->real_count++ ] = value;
}

void BardEventQueue_write_real( BardEventQueue* queue, BardReal value )
{
  BardEventQueue_ensure_additional_real_capacity( queue, 1 );
  queue->real_data[ queue->real_count++ ] = value;
}

void BardEventQueue_write_byte( BardEventQueue* queue, BardByte value )
{
  BardEventQueue_ensure_additional_character_capacity( queue, 1 );
  queue->character_data[ queue->character_count++ ] = value;
}

void BardEventQueue_write_bytes( BardEventQueue* queue, BardByte* data, int count )
{
  int i;
  BardCharacter* dest;

  BardEventQueue_write_integer( queue, count );
  BardEventQueue_ensure_additional_character_capacity( queue, count );

  dest = queue->character_data + queue->character_count;
  queue->character_count += count;

  for (i=0; i<count; ++i) dest[i] = data[i];
}

void BardEventQueue_write_characters( BardEventQueue* queue, BardCharacter* data, int count )
{
  BardEventQueue_write_integer( queue, count );
  BardEventQueue_ensure_additional_character_capacity( queue, count );

  memcpy( queue->character_data, data, count * sizeof(BardCharacter) );

  queue->character_count += count;
}



