#ifndef BARD_EVENT_QUEUE_H
#define BARD_EVENT_QUEUE_H

//=============================================================================
// BardEventQueue
//=============================================================================
typedef struct BardEventQueue
{
  BardXCCharacter* character_data;
  int            character_capacity;
  int            character_count;

  BardXCReal*      real_data;
  int            real_capacity;
  int            real_count;
} BardEventQueue;

BardEventQueue* BardEventQueue_create();
BardEventQueue* BardEventQueue_destroy( BardEventQueue* queue );

void BardEventQueue_clear( BardEventQueue* queue );

void BardEventQueue_ensure_additional_character_capacity( BardEventQueue* queue, int additional_character_capacity );
void BardEventQueue_ensure_additional_real_capacity( BardEventQueue* queue, int additional_real_capacity );

void BardEventQueue_write_integer( BardEventQueue* queue, BardXCInteger value );
void BardEventQueue_write_real( BardEventQueue* queue, BardXCReal value );

void BardEventQueue_write_byte( BardEventQueue* queue, BardXCByte value );
void BardEventQueue_write_bytes( BardEventQueue* queue, BardXCByte* data, int count );
void BardEventQueue_write_characters( BardEventQueue* queue, BardXCCharacter* data, int count );

#endif // BARD_MESSAGE_QUEUE_H

