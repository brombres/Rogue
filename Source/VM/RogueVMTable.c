//=============================================================================
//  RogueVMTable.c
//
//  2015.09.06 by Abe Pralle
//=============================================================================
#include "Rogue.h"

//-----------------------------------------------------------------------------
//  VMTableEntry
//-----------------------------------------------------------------------------
RogueVMTableEntry* RogueVMTableEntry_create( RogueVM* vm, RogueString* key, void* value )
{
  RogueVMTableEntry* THIS = RogueAllocator_allocate( &vm->allocator, sizeof(RogueVMTableEntry) );
  THIS->key = key;
  THIS->hash_code = key->hash_code;
  THIS->value = value;
  return THIS;
}

RogueVMTableEntry* RogueVMTableEntry_delete( RogueVMTableEntry* THIS )
{
  RogueVM* vm = THIS->key->object.type->vm;
  RogueAllocator_free( &vm->allocator, THIS );
  return 0;
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
RogueVMTable* RogueVMTable_create( RogueVM* vm, RogueInteger initial_bin_count )
{
  RogueVMTable*  table = RogueAllocator_allocate( &vm->allocator, sizeof(RogueVMTable) );
  RogueInteger bin_count = 1;

  table->vm = vm;

  if (initial_bin_count <= 0) initial_bin_count = 16;

  // Ensure the bin count is a power of 2
  while (bin_count < initial_bin_count) bin_count <<= 1;

  table->bin_count = bin_count;
  table->bin_mask = bin_count - 1;
  table->bins = RogueAllocator_allocate( &vm->allocator, bin_count * sizeof(void*) );

  return table;
}

RogueVMTable* RogueVMTable_delete( RogueVMTable* THIS )
{
  RogueAllocator_free( &THIS->vm->allocator, THIS->bins );
  RogueAllocator_free( &THIS->vm->allocator, THIS );
  return 0;
}

RogueVMTableEntry* RogueVMTable_find( void* table, RogueString* key, int create_if_necessary )
{
  RogueVMTable*      THIS = table;
  RogueInteger       hash_code = key->hash_code;
  RogueInteger       bindex = hash_code & THIS->bin_mask;
  RogueVMTableEntry* cur = THIS->bins->objects[ bindex ];

  while (cur)
  {
    if (cur->hash_code == hash_code && ((RogueObject*)cur->key)->type->equals(cur->key,key))
    {
      return cur;
    }
    cur = cur->next_entry;
  }

  if (create_if_necessary)
  {
    RogueVM* vm = THIS->vm;
    cur = RogueVMTableEntry_create( vm, RogueVM_consolidate_string(vm,key), 0 );
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

RogueVMTableEntry* RogueVMTable_find_c_string( void* table, const char* key, int create_if_necessary )
{
  RogueVMTable*      THIS = table;
  RogueInteger       hash_code = RogueString_calculate_hash_code( key );
  RogueInteger       bindex = hash_code & THIS->bin_mask;
  RogueVMTableEntry* cur = THIS->bins->objects[ bindex ];

  while (cur)
  {
    if (cur->hash_code == hash_code && ((RogueObject*)cur->key)->type->equals_c_string(cur->key,key))
    {
      return cur;
    }
    cur = cur->next_entry;
  }

  if (create_if_necessary)
  {
    RogueVM* vm = THIS->vm;
    cur = RogueVMTableEntry_create( vm, RogueVM_consolidate_c_string(vm,key), 0 );
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

void* RogueVMTable_get( void* THIS, RogueString* key )
{
  RogueVMTableEntry* entry = RogueVMTable_find( THIS, key, 0 );
  if (entry) return entry->value;
  return 0;
}

void* RogueVMTable_get_c_string( void* THIS, const char* key )
{
  RogueVMTableEntry* entry = RogueVMTable_find_c_string( THIS, key, 0 );
  if (entry) return entry->value;
  return 0;
}

RogueVMTable* RogueVMTable_set( void* THIS, RogueString* key, void* value )
{
  RogueVMTable_find( THIS, key, 1 )->value = value;
  return THIS;
}

RogueVMTable* RogueVMTable_set_c_string( void* THIS, const char* key, void* value )
{
  RogueVMTable_find_c_string( THIS, key, 1 )->value = value;
  return THIS;
}

