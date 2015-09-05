//=============================================================================
//  RogueTable.c
//
//  2015.09.02 by Abe Pralle
//=============================================================================
#include "Rogue.h"

//-----------------------------------------------------------------------------
//  TableEntry
//-----------------------------------------------------------------------------
RogueType* RogueTypeTableEntry_create( RogueVM* vm )
{
  RogueType* THIS = RogueType_create( vm, "TableEntry", sizeof(RogueTableEntry) );
  return THIS;
}

RogueTableEntry* RogueTableEntry_create( RogueVM* vm, RogueInteger hash_code,
                                         void* key, void* value )
{
  RogueTableEntry* THIS = (RogueTableEntry*) RogueObject_create( vm->type_TableEntry, -1 );
  THIS->hash_code = hash_code;
  THIS->key = key;
  THIS->value = value;
  return THIS;
}

//-----------------------------------------------------------------------------
//  TableReader
//-----------------------------------------------------------------------------
void RogueTableReader_init( RogueTableReader* reader, RogueTable* table )
{
  reader->table = table;
  reader->bin_index  = -1;
  reader->index = -1;
  reader->next_entry = RogueTableReader_advance( reader, 0 );
}

RogueTableEntry* RogueTableReader_advance( RogueTableReader* reader, RogueTableEntry* cur )
{
  RogueArray*  bins;

  if (++reader->index >= reader->table->count) return 0;

  if (cur)
  {
    cur = cur->next_entry;
    if (cur) return cur;
  }

  bins = reader->table->bins;
  while ( !cur )
  {
    cur = (RogueTableEntry*) bins->objects[ ++reader->bin_index ];
  }

  return cur;
}

int  RogueTableReader_has_another( RogueTableReader* reader )
{
  return !!reader->next_entry;
}

RogueTableEntry* RogueTableReader_read( RogueTableReader* reader )
{
  RogueTableEntry* cur_entry = reader->next_entry;
  reader->next_entry = RogueTableReader_advance( reader, cur_entry );
  return cur_entry;
}


//-----------------------------------------------------------------------------
//  Table
//-----------------------------------------------------------------------------
RogueType* RogueTypeTable_create( RogueVM* vm )
{
  RogueType* table = RogueType_create( vm, "Table", sizeof(RogueTable) );
  table->print = RoguePrintFn_table;
  return table;
}

void RoguePrintFn_table( void* table, RogueStringBuilder* builder )
{
  RogueTableReader reader;
  int first = 1;

  RogueStringBuilder_print_character( builder, '{' );

  RogueTableReader_init( &reader, table );
  while (RogueTableReader_has_another(&reader))
  {
    RogueTableEntry* entry = RogueTableReader_read( &reader );
    if (first) first = 0;
    else       RogueStringBuilder_print_character( builder, ',' );
    RogueStringBuilder_print_object( builder, entry->key );
    RogueStringBuilder_print_character( builder, ':' );
    RogueStringBuilder_print_object( builder, entry->value );
  }

  RogueStringBuilder_print_character( builder, '}' );
}

RogueTable* RogueTable_create( RogueVM* vm, RogueInteger initial_bin_count )
{
  RogueTable*  table = RogueObject_create( vm->type_Table, -1 );
  RogueInteger bin_count = 1;

  if (initial_bin_count <= 0) initial_bin_count = 16;

  // Ensure the bin count is a power of 2
  while (bin_count < initial_bin_count) bin_count <<= 1;

  table->bin_count = bin_count;
  table->bin_mask = bin_count - 1;
  table->bins = RogueObjectArray_create( vm, bin_count );

  return table;
}

RogueTableEntry* RogueTable_find_c_string( void* table, const char* key, int create_if_necessary )
{
  RogueTable*      THIS = table;
  RogueInteger     hash_code = RogueString_calculate_hash_code( key );
  RogueInteger     bindex = hash_code & THIS->bin_mask;
  RogueTableEntry* cur = THIS->bins->objects[ bindex ];

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
    RogueVM* vm = THIS->object.type->vm;
    cur = RogueTableEntry_create( vm, hash_code, RogueString_create_from_c_string(vm,key), 0 );
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

RogueTableEntry* RogueTable_find_characters( void* table, RogueInteger hash_code,
    RogueCharacter* characters, RogueInteger count, int create_if_necessary )
{
  RogueTable*      THIS = table;
  RogueInteger     bindex = hash_code & THIS->bin_mask;
  RogueTableEntry* cur = THIS->bins->objects[ bindex ];

  while (cur)
  {
    if (cur->hash_code == hash_code && ((RogueObject*)cur->key)->type->equals_characters(cur->key,hash_code,characters,count))
    {
      return cur;
    }
    cur = cur->next_entry;
  }

  if (create_if_necessary)
  {
    RogueVM* vm = THIS->object.type->vm;
    cur = RogueTableEntry_create( vm, hash_code, RogueString_create_from_characters(vm,characters,count), 0 );
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

RogueTable* RogueTable_set_c_string_to_c_string( void* THIS, const char* key, const char* value )
{
  return RogueTable_set_c_string_to_object( THIS, key,
      RogueString_create_from_c_string( ((RogueObject*)THIS)->type->vm, value ) );
}

RogueTable* RogueTable_set_c_string_to_object( void* THIS, const char* key, void* value )
{
  RogueTableEntry* entry = RogueTable_find_c_string( THIS, key, 1 );
  entry->value = value;
  return THIS;
}

