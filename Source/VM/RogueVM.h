//=============================================================================
//  RogueVM.h
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_VM_H
#define ROGUE_VM_H

struct RogueVM
{
  RogueAllocator     allocator;
  RogueAllocation*   allocations; // internal allocations
  RogueObject*       objects;     // runtime objects
  RogueTable*        consolidation_table;
  RogueList*         c_string_buffer;
  RogueList*         global_commands;

  RogueType*         type_ByteArray;
  RogueType*         type_ByteList;
  RogueType*         type_CharacterArray;
  RogueType*         type_ObjectArray;
  RogueType*         type_ObjectList;
  RogueType*         type_ParseReader;
  RogueType*         type_String;
  RogueType*         type_Table;
  RogueType*         type_TableEntry;
  RogueType*         type_Tokenizer;

  RogueCmdType*      cmd_type_eol;
};

RogueVM* RogueVM_create();
RogueVM* RogueVM_delete( RogueVM* THIS );

void* RogueVM_allocate( RogueVM* THIS, RogueInteger size );

RogueString* RogueVM_consolidate_string( RogueVM* THIS, RogueString* st );
RogueString* RogueVM_consolidate_characters( RogueVM* THIS, RogueCharacter* characters, RogueInteger count );
RogueString* RogueVM_consolidate_c_string( RogueVM* THIS, const char* utf8 );

//int RogueVM_parse( RogueVM* THIS, const char* utf8 );

#endif // ROGUE_VM_H

