#ifndef BARD_EVENT_QUEUE_H
#define BARD_EVENT_QUEUE_H

//=============================================================================
// BardEventQueue
//=============================================================================
typedef struct BardEventQueue
{
  BardCharacter* character_data;
  int            character_capacity;
  int            character_count;

  BardReal*      real_data;
  int            real_capacity;
  int            real_count;
} BardEventQueue;

BardEventQueue* BardEventQueue_create();
BardEventQueue* BardEventQueue_destroy( BardEventQueue* queue );

void BardEventQueue_clear( BardEventQueue* queue );

void BardEventQueue_ensure_additional_character_capacity( BardEventQueue* queue, int additional_character_capacity );
void BardEventQueue_ensure_additional_real_capacity( BardEventQueue* queue, int additional_real_capacity );

void BardEventQueue_write_integer( BardEventQueue* queue, BardInteger value );
void BardEventQueue_write_real( BardEventQueue* queue, BardReal value );

void BardEventQueue_write_byte( BardEventQueue* queue, BardByte value );
void BardEventQueue_write_bytes( BardEventQueue* queue, BardByte* data, int count );
void BardEventQueue_write_characters( BardEventQueue* queue, BardCharacter* data, int count );

#endif // BARD_MESSAGE_QUEUE_H

