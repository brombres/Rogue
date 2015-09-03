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
  RogueAllocator allocator;
  RogueObject*   objects;

  RogueType*     type_ObjectArray;
  RogueType*     type_String;
  RogueType*     type_Table;
  RogueType*     type_TableEntry;
};

RogueVM* RogueVM_create();
RogueVM* RogueVM_delete( RogueVM* THIS );

RogueString* RogueVM_consolidate( RogueVM* THIS, RogueString* st );
RogueString* RogueVM_consolidate_characters( RogueVM* THIS, RogueCharacter* characters, RogueInteger count );
RogueString* RogueVM_consolidate_utf8( RogueVM* THIS, const char* utf8, int utf8_count );

#endif // ROGUE_VM_H

