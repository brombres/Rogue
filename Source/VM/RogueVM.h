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
  RogueAllocation*   vm_objects;
  void*              objects;     // runtime objects
  RogueTable*        consolidation_table;
  RogueList*         c_string_buffer;
  RogueList*         global_commands;

  RogueErrorHandler* error_handler;
  RogueString*       error_filepath;
  RogueParsePosition error_position;
  RogueStringBuilder error_message_builder;

  RogueVMList*       type_list;
  RogueVMTable*      type_lookup;

  RogueType*         type_ByteArray;
  RogueType*         type_ByteList;
  RogueType*         type_CharacterArray;
  RogueType*         type_Integer;
  RogueType*         type_ObjectArray;
  RogueType*         type_ObjectList;
  RogueType*         type_ParseReader;
  RogueType*         type_String;
  RogueType*         type_Table;
  RogueType*         type_TableEntry;

  RogueCmdType*      cmd_type_eol;
  RogueCmdType*      cmd_type_list;
  RogueCmdType*      cmd_type_literal_integer;
  RogueCmdType*      cmd_type_symbol_close_paren;
  RogueCmdType*      cmd_type_symbol_exclamation;
  RogueCmdType*      cmd_type_symbol_eq;
  RogueCmdType*      cmd_type_symbol_equals;
  RogueCmdType*      cmd_type_symbol_ge;
  RogueCmdType*      cmd_type_symbol_gt;
  RogueCmdType*      cmd_type_symbol_le;
  RogueCmdType*      cmd_type_symbol_lt;
  RogueCmdType*      cmd_type_symbol_ne;
  RogueCmdType*      cmd_type_symbol_open_paren;
  RogueCmdType*      cmd_type_symbol_plus;
  RogueCmdType*      cmd_type_symbol_pound;
};

RogueVM* RogueVM_create();
RogueVM* RogueVM_delete( RogueVM* THIS );

RogueString* RogueVM_consolidate_string( RogueVM* THIS, RogueString* st );
RogueString* RogueVM_consolidate_characters( RogueVM* THIS, RogueCharacter* characters, RogueInteger count );
RogueString* RogueVM_consolidate_c_string( RogueVM* THIS, const char* utf8 );

RogueType*   RogueVM_create_type( RogueVM* THIS, RogueCmd* origin, const char* name, RogueInteger object_size );

RogueString* RogueVM_error_string( RogueVM* THIS );
void         RogueVM_log_error( RogueVM* THIS );

RogueLogical RogueVM_load_file( RogueVM* THIS, const char* filepath );



//int RogueVM_parse( RogueVM* THIS, const char* utf8 );

#endif // ROGUE_VM_H

