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
  RogueTableEntry* THIS = (RogueTableEntry*) RogueType_create_object( vm->type_TableEntry, -1 );
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

  return 0;
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

void RoguePrintFn_table( void* table_ptr, RogueStringBuilder* builder )
{
  //int i;
  //RogueTable* table = table_ptr;
  RogueStringBuilder_print_character( builder, '{' );
  RogueStringBuilder_print_c_string( builder, "..." );

  //for (i=0; i<table->count

  RogueStringBuilder_print_character( builder, '}' );
}

RogueTable* RogueTable_create( RogueVM* vm, RogueInteger initial_bin_count )
{
  RogueTable*  table = RogueType_create_object( vm->type_Table, -1 );
  RogueInteger bin_count = 1;

  // Ensure the bin count is a power of 2
  while (bin_count < initial_bin_count) bin_count <<= 1;

  table->bin_count = bin_count;
  table->bin_mask = bin_count - 1;
  table->bins = RogueObjectArray_create( vm, bin_count );

  return table;
}

