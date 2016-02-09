//=============================================================================
//  RogueTable.h
//
//  2015.09.02 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_TABLE_H
#define ROGUE_TABLE_H

//-----------------------------------------------------------------------------
//  TableEntry
//-----------------------------------------------------------------------------
RogueType* RogueTypeTableEntry_create( RogueVM* vm );

struct RogueTableEntry
{
  RogueObject      object;
  void*            key;
  void*            value;
  RogueTableEntry* next_entry;
  RogueInteger     hash_code;
};

RogueTableEntry* RogueTableEntry_create( RogueVM* vm, RogueInteger hash_code,
                                         void* key, void* value );


//-----------------------------------------------------------------------------
//  TableReader
//-----------------------------------------------------------------------------
typedef struct RogueTableReader
{
  RogueTable*      table;
  RogueTableEntry* next_entry;
  RogueInteger     bin_index;
  RogueInteger     index;
} RogueTableReader;

void             RogueTableReader_init( RogueTableReader* reader, RogueTable* table );
RogueTableEntry* RogueTableReader_advance( RogueTableReader* reader, RogueTableEntry* cur );
int              RogueTableReader_has_another( RogueTableReader* reader );
RogueTableEntry* RogueTableReader_read( RogueTableReader* reader );


//-----------------------------------------------------------------------------
//  Table
//-----------------------------------------------------------------------------
RogueType* RogueTypeTable_create( RogueVM* vm );
void       RoguePrintFn_table( void* table, RogueStringBuilder* builder );

struct RogueTable
{
  RogueObject object;

  RogueArray*  bins;
  RogueInteger bin_count;
  RogueInteger bin_mask;
  RogueInteger count;
};

RogueTable* RogueTable_create( RogueVM* vm, RogueInteger initial_bin_count );

RogueTableEntry* RogueTable_find_c_string( void* table, const char* key, int create_if_necessary );
RogueTableEntry* RogueTable_find_characters( void* table, RogueInteger hash_code,
    RogueCharacter* characters, RogueInteger count, int create_if_necessary );
RogueTable*      RogueTable_set_c_string_to_c_string( void* THIS, const char* key, const char* value );
RogueTable*      RogueTable_set_c_string_to_object( void* THIS, const char* key, void* value );

#endif // ROGUE_TABLE_H
