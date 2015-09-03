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
  RogueObject*     key;
  RogueObject*     value;
  RogueTableEntry* next_entry;
  RogueInteger     hash_code;
};

RogueTableEntry* RogueTableEntry_create( RogueVM* vm, RogueInteger hash_code,
                                         RogueObject* key, RogueObject* value );

//-----------------------------------------------------------------------------
//  Table
//-----------------------------------------------------------------------------
RogueType* RogueTypeTable_create( RogueVM* vm );

struct RogueTable
{
  RogueObject object;
};

#endif // ROGUE_TABLE_H
