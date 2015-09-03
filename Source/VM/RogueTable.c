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
                                         RogueObject* key, RogueObject* value )
{
  RogueTableEntry* THIS = (RogueTableEntry*) RogueType_create_object( vm->type_TableEntry, -1 );
  THIS->hash_code = hash_code;
  THIS->key = key;
  THIS->value = value;
  return THIS;
}

//-----------------------------------------------------------------------------
//  Table
//-----------------------------------------------------------------------------
RogueType* RogueTypeTable_create( RogueVM* vm )
{
  RogueType* THIS = RogueType_create( vm, "Table", sizeof(RogueTable) );
  return THIS;
}


