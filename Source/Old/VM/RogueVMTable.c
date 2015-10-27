//=============================================================================
//  RogueVMTable.c
//
//  2015.09.06 by Abe Pralle
//=============================================================================
#include "Rogue.h"

//-----------------------------------------------------------------------------
//  VMTableEntry
//-----------------------------------------------------------------------------
RogueVMTableEntry* RogueVMTableEntry_create( RogueVM* vm, const char* key, void* value )
{
  RogueInteger key_length = (RogueInteger) strlen( key );
  RogueVMTableEntry* THIS = RogueVMObject_create( vm, sizeof(RogueVMTableEntry) + (key_length+1) );
  THIS->vm = vm;
  THIS->hash_code = RogueString_calculate_hash_code( key );
  THIS->value = value;
  THIS->key_length = key_length;
  strcpy( THIS->key, key );
  return THIS;
}

//-----------------------------------------------------------------------------
//  VMTableReader
//-----------------------------------------------------------------------------
void RogueVMTableReader_init( RogueVMTableReader* reader, RogueVMTable* table )
{
  reader->table = table;
  reader->bindex  = -1;
  reader->index = -1;
  reader->next_entry = RogueVMTableReader_advance( reader, 0 );
}

RogueVMTableEntry* RogueVMTableReader_advance( RogueVMTableReader* reader, RogueVMTableEntry* cur )
{
  RogueVMArray* bins;

  if (++reader->index >= reader->table->count) return 0;

  if (cur)
  {
    cur = cur->next_entry;
    if (cur) return cur;
  }

  bins = reader->table->bins;
  while ( !cur )
  {
    cur = (RogueVMTableEntry*) bins->objects[ ++reader->bindex ];
  }

  return cur;
}

int  RogueVMTableReader_has_another( RogueVMTableReader* reader )
{
  return !!reader->next_entry;
}

RogueVMTableEntry* RogueVMTableReader_read( RogueVMTableReader* reader )
{
  RogueVMTableEntry* cur_entry = reader->next_entry;
  reader->next_entry = RogueVMTableReader_advance( reader, cur_entry );
  return cur_entry;
}


//-----------------------------------------------------------------------------
//  VMTable
//-----------------------------------------------------------------------------
RogueVMTable* RogueVMTable_create( RogueVM* vm, RogueInteger initial_bin_count,
   RogueVMTraceFn trace_fn )
{
  RogueVMTable*  table = RogueVMObject_create( vm, sizeof(RogueVMTable) );
  RogueInteger bin_count = 1;

  table->vm = vm;
  table->trace_fn = trace_fn;

  if (initial_bin_count <= 0) initial_bin_count = 16;

  // Ensure the bin count is a power of 2
  while (bin_count < initial_bin_count) bin_count <<= 1;

  table->bin_count = bin_count;
  table->bin_mask = bin_count - 1;
  table->bins = RogueVMObject_create( vm, bin_count * sizeof(void*) );

  return table;
}

void RogueVMTable_clear( void* table )
{
  RogueVMTable*      THIS = table;

  THIS->count = 0;
  memset( THIS->bins->objects, 0, THIS->bin_count * sizeof(void*) );
}

RogueVMTableEntry* RogueVMTable_find( void* table, const char* key, int create_if_necessary )
{
  RogueVMTable*      THIS = table;
  RogueInteger       hash_code = RogueString_calculate_hash_code( key );
  RogueInteger       bindex = hash_code & THIS->bin_mask;
  RogueVMTableEntry* cur = THIS->bins->objects[ bindex ];

  while (cur)
  {
    if (cur->hash_code == hash_code && 0 == strcmp(cur->key,key))
    {
      return cur;
    }
    cur = cur->next_entry;
  }

  if (create_if_necessary)
  {
    RogueVM* vm = THIS->vm;
    cur = RogueVMTableEntry_create( vm, key, 0 );
    cur->next_entry = THIS->bins->objects[ bindex ];
    THIS->bins->objects[ bindex ] = cur;
    ++THIS->count;
    return cur;
  }
  else
  {
    return 0;
  }
}

void* RogueVMTable_get( void* THIS, const char* key )
{
  RogueVMTableEntry* entry = RogueVMTable_find( THIS, key, 0 );
  if (entry) return entry->value;
  return 0;
}

RogueVMTable* RogueVMTable_set( void* THIS, const char* key, void* value )
{
  RogueVMTable_find( THIS, key, 1 )->value = value;
  return THIS;
}

void RogueVMTable_trace( RogueVMTable* THIS )
{
  if (THIS && THIS->allocation.size >= 0)
  {
    RogueVMTableReader reader;
    RogueVMTraceFn trace_fn = THIS->trace_fn;
    THIS->allocation.size ^= -1;

    THIS->bins->allocation.size ^= -1;

    RogueVMTableReader_init( &reader, THIS );
    while (RogueVMTableReader_has_another(&reader))
    {
      RogueVMTableEntry* entry = RogueVMTableReader_read( &reader );
      entry->allocation.size ^= -1;
      trace_fn( entry->value );
    }
  }
}
