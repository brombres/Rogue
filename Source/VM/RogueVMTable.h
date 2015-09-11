//=============================================================================
//  RogueVMTable.h
//
//  2015.09.06 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_VM_TABLE_H
#define ROGUE_VM_TABLE_H

//-----------------------------------------------------------------------------
//  VMTableEntry
//-----------------------------------------------------------------------------
struct RogueVMTableEntry
{
  RogueAllocation    allocation;
  RogueInteger       hash_code;
  RogueInteger       key_length;
  RogueVM*           vm;
  RogueVMTableEntry* next_entry;
  void*              value;
  char               key[0];
};

RogueVMTableEntry* RogueVMTableEntry_create( RogueVM* vm, const char* key, void* value );


//-----------------------------------------------------------------------------
//  VMTableReader
//-----------------------------------------------------------------------------
struct RogueVMTableReader
{
  RogueVMTable*      table;
  RogueVMTableEntry* next_entry;
  RogueInteger       bindex;
  RogueInteger       index;
};

void               RogueVMTableReader_init( RogueVMTableReader* reader, RogueVMTable* table );
RogueVMTableEntry* RogueVMTableReader_advance( RogueVMTableReader* reader, RogueVMTableEntry* cur );
int                RogueVMTableReader_has_another( RogueVMTableReader* reader );
RogueVMTableEntry* RogueVMTableReader_read( RogueVMTableReader* reader );


//-----------------------------------------------------------------------------
//  VMTable
//-----------------------------------------------------------------------------
struct RogueVMTable
{
  RogueAllocation allocation;
  RogueVM*        vm;
  RogueVMArray*   bins;
  RogueInteger    bin_count;
  RogueInteger    bin_mask;
  RogueInteger    count;
  RogueVMTraceFn  trace_fn;
};

RogueVMTable* RogueVMTable_create( RogueVM* vm, RogueInteger initial_bin_count, RogueVMTraceFn trace_fn );

void               RogueVMTable_clear( void* THIS );
RogueVMTableEntry* RogueVMTable_find( void* THIS, const char* key, int create_if_necessary );
void*              RogueVMTable_get( void* THIS, const char* key );
RogueVMTable*      RogueVMTable_set( void* THIS, const char* key, void* value );
void               RogueVMTable_trace( RogueVMTable* THIS );

#endif // ROGUE_VM_TABLE_H
