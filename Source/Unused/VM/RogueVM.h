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

  RogueLogical       have_new_vm_objects;
  RogueAllocation*   vm_objects;
  void*              objects;     // runtime objects
  RogueTable*        consolidation_table;
  RogueList*         c_string_buffer;
  RogueCmdList*      immediate_commands;
  RogueLogical       is_resolved;

  RogueLogical       has_error;
  RogueErrorHandler* error_handler;
  RogueString*       error_filepath;
  RogueStringBuilder error_message_builder;

  RogueVMList*       type_list;
  RogueVMTable*      type_lookup;

  RogueType*         type_ByteArray;
  RogueType*         type_ByteList;
  RogueType*         type_CharacterArray;
  RogueType*         type_Integer;
  RogueType*         type_ObjectArray;
  RogueType*         type_ObjectList;
  RogueType*         type_ETCReader;
  RogueType*         type_Real;
  RogueType*         type_String;
  RogueType*         type_Table;
  RogueType*         type_TableEntry;
};

RogueVM* RogueVM_create();
RogueVM* RogueVM_delete( RogueVM* THIS );

void         RogueVM_collect_garbage( RogueVM* THIS );

RogueString* RogueVM_consolidate_string( RogueVM* THIS, RogueString* st );
RogueString* RogueVM_consolidate_characters( RogueVM* THIS, RogueCharacter* characters, RogueInteger count );
RogueString* RogueVM_consolidate_c_string( RogueVM* THIS, const char* utf8 );

RogueType*   RogueVM_create_type( RogueVM* THIS, RogueCmd* origin, const char* name, RogueInteger object_size );

RogueString* RogueVM_error_string( RogueVM* THIS );
RogueType*   RogueVM_find_type( RogueVM* THIS, const char* name );
void         RogueVM_log_error( RogueVM* THIS );

RogueLogical RogueVM_load_file( RogueVM* THIS, const char* filepath );
RogueLogical RogueVM_resolve( RogueVM* THIS );
RogueLogical RogueVM_launch( RogueVM* THIS );



//int RogueVM_parse( RogueVM* THIS, const char* utf8 );

#endif // ROGUE_VM_H

