#include "BardXC.h"

#include <stdlib.h>
#include <string.h>

//=============================================================================
// BardEventQueue
//=============================================================================
BardEventQueue* BardEventQueue_create()
{
  BardEventQueue* queue = (BardEventQueue*) BARD_ALLOCATE( sizeof(BardEventQueue) );

  queue->character_capacity = 256;
  queue->character_data = (BardXCCharacter*) BARD_ALLOCATE( queue->character_capacity * sizeof(BardXCCharacter) );
  queue->character_count = 0;

  queue->real_capacity = 64;
  queue->real_data = (BardXCReal*) BARD_ALLOCATE( queue->real_capacity * sizeof(BardXCReal) );
  queue->real_count = 0;

  return queue;
}

BardEventQueue* BardEventQueue_destroy( BardEventQueue* queue )
{
  if ( !queue ) return NULL;

  BARD_FREE( queue->character_data );
  BARD_FREE( queue->real_data );
  BARD_FREE( queue );

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
    BardXCCharacter* new_data;
    int desired = queue->character_capacity * 2;
    if (necessary > desired) desired = necessary;

    new_data = (BardXCCharacter*) BARD_ALLOCATE( desired * sizeof(BardXCCharacter) );
    memcpy( new_data, queue->character_data, queue->character_count * sizeof(BardXCCharacter) );
    BARD_FREE( queue->character_data );
    queue->character_data = new_data;
  }
}

void BardEventQueue_ensure_additional_real_capacity( BardEventQueue* queue, int additional_character_capacity )
{
  int necessary = queue->real_count + additional_character_capacity;
  if (necessary > queue->real_capacity)
  {
    BardXCReal* new_data;
    int desired = queue->real_capacity * 2;
    if (necessary > desired) desired = necessary;

    new_data = (BardXCReal*) BARD_ALLOCATE( desired * sizeof(BardXCReal) );
    memcpy( new_data, queue->real_data, queue->real_count * sizeof(BardXCReal) );
    BARD_FREE( queue->real_data );
    queue->real_data = new_data;
  }
}

void BardEventQueue_write_integer( BardEventQueue* queue, BardXCInteger value )
{
  BardEventQueue_ensure_additional_real_capacity( queue, 1 );
  queue->real_data[ queue->real_count++ ] = value;
}

void BardEventQueue_write_real( BardEventQueue* queue, BardXCReal value )
{
  BardEventQueue_ensure_additional_real_capacity( queue, 1 );
  queue->real_data[ queue->real_count++ ] = value;
}

void BardEventQueue_write_byte( BardEventQueue* queue, BardXCByte value )
{
  BardEventQueue_ensure_additional_character_capacity( queue, 1 );
  queue->character_data[ queue->character_count++ ] = value;
}

void BardEventQueue_write_bytes( BardEventQueue* queue, BardXCByte* data, int count )
{
  int i;
  BardXCCharacter* dest;

  BardEventQueue_write_integer( queue, count );
  BardEventQueue_ensure_additional_character_capacity( queue, count );

  dest = queue->character_data + queue->character_count;
  queue->character_count += count;

  for (i=0; i<count; ++i) dest[i] = data[i];
}

void BardEventQueue_write_characters( BardEventQueue* queue, BardXCCharacter* data, int count )
{
  BardEventQueue_write_integer( queue, count );
  BardEventQueue_ensure_additional_character_capacity( queue, count );

  memcpy( queue->character_data, data, count * sizeof(BardXCCharacter) );

  queue->character_count += count;
}



