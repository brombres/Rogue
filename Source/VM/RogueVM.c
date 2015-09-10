//=============================================================================
//  RogueVM.c
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#include "Rogue.h"

RogueVM* RogueVM_create()
{
  RogueVM* THIS = (RogueVM*) malloc( sizeof(RogueVM) );
  memset( THIS, 0, sizeof(RogueVM) );
  RogueAllocator_init( THIS, &THIS->allocator );

  RogueStringBuilder_init( &THIS->error_message_builder, THIS, -1 );

  THIS->type_list   = RogueVMList_create( THIS, 50, RogueVMTraceType );
  THIS->type_lookup = RogueVMTable_create( THIS, 64 );

  THIS->type_ByteArray      = RogueTypeByteArray_create( THIS );
  THIS->type_ByteList       = RogueTypeByteList_create( THIS );
  THIS->type_CharacterArray = RogueTypeCharacterArray_create( THIS );
  THIS->type_Integer        = RogueVM_create_type( THIS, 0, "Integer", sizeof(RogueInteger) );
  THIS->type_ObjectArray    = RogueTypeObjectArray_create( THIS );
  THIS->type_ObjectList     = RogueTypeObjectList_create( THIS );
  THIS->type_ParseReader    = RogueTypeParseReader_create( THIS );
  THIS->type_String         = RogueTypeString_create( THIS );
  THIS->type_Table          = RogueTypeTable_create( THIS );
  THIS->type_TableEntry     = RogueTypeTableEntry_create( THIS );

  THIS->c_string_buffer = RogueByteList_create( THIS, 200 );
  THIS->global_commands = RogueVMList_create( THIS, 20, RogueVMTraceCmd );
  THIS->consolidation_table = RogueTable_create( THIS, 128 );

  THIS->cmd_type_eol  = RogueCmdType_create( THIS, ROGUE_CMD_EOL, "[end of line]", sizeof(RogueCmd) );
  THIS->cmd_type_statement_list = RogueCmdType_create( THIS, ROGUE_CMD_STATEMENT_LIST, "", sizeof(RogueCmdStatementList) );

  THIS->cmd_type_literal_integer = RogueCmdType_create( THIS, ROGUE_CMD_LITERAL_INTEGER,
      "[integer]", sizeof(RogueCmdLiteralInteger) );

  THIS->cmd_type_symbol_close_paren = RogueCmdType_create( THIS, ROGUE_CMD_SYMBOL_CLOSE_PAREN, ")", sizeof(RogueCmd) );
  THIS->cmd_type_symbol_eq = RogueCmdType_create( THIS, ROGUE_CMD_SYMBOL_EQ, "==", sizeof(RogueCmd) );
  THIS->cmd_type_symbol_equals = RogueCmdType_create( THIS, ROGUE_CMD_SYMBOL_EQUALS, "=", sizeof(RogueCmd) );
  THIS->cmd_type_symbol_exclamation = RogueCmdType_create( THIS, ROGUE_CMD_SYMBOL_EXCLAMATION, "!", sizeof(RogueCmd) );
  THIS->cmd_type_symbol_ge = RogueCmdType_create( THIS, ROGUE_CMD_SYMBOL_GE, ">=", sizeof(RogueCmd) );
  THIS->cmd_type_symbol_gt = RogueCmdType_create( THIS, ROGUE_CMD_SYMBOL_GT, ">", sizeof(RogueCmd) );
  THIS->cmd_type_symbol_le = RogueCmdType_create( THIS, ROGUE_CMD_SYMBOL_LE, ">=", sizeof(RogueCmd) );
  THIS->cmd_type_symbol_lt = RogueCmdType_create( THIS, ROGUE_CMD_SYMBOL_LT, "<", sizeof(RogueCmd) );
  THIS->cmd_type_symbol_ne = RogueCmdType_create( THIS, ROGUE_CMD_SYMBOL_NE, "!=", sizeof(RogueCmd) );
  THIS->cmd_type_symbol_open_paren = RogueCmdType_create( THIS, ROGUE_CMD_SYMBOL_OPEN_PAREN, "(", sizeof(RogueCmd) );
  THIS->cmd_type_symbol_plus = RogueCmdType_create( THIS, ROGUE_CMD_SYMBOL_PLUS, "+", sizeof(RogueCmdBinaryOp) );
  THIS->cmd_type_symbol_pound = RogueCmdType_create( THIS, ROGUE_CMD_SYMBOL_POUND, "#", sizeof(RogueCmd) );

  return THIS;
}

RogueVM* RogueVM_delete( RogueVM* THIS )
{
  if (THIS)
  {
    RogueType_delete( THIS->type_ByteArray );
    RogueType_delete( THIS->type_ByteList );
    RogueType_delete( THIS->type_CharacterArray );
    RogueType_delete( THIS->type_Integer );
    RogueType_delete( THIS->type_ObjectArray );
    RogueType_delete( THIS->type_ObjectList );
    RogueType_delete( THIS->type_ParseReader );
    RogueType_delete( THIS->type_String );
    RogueType_delete( THIS->type_Table );
    RogueType_delete( THIS->type_TableEntry );

    RogueStringBuilder_retire( &THIS->error_message_builder );
    RogueAllocator_retire( &THIS->allocator );
    free( THIS );
  }
  return 0;
}

void RogueVM_collect_garbage( RogueVM* THIS )
{
  if (THIS->have_new_vm_objects)
  {
printf( "GC system objects\n" );

    RogueAllocation* survivors = 0;
    THIS->have_new_vm_objects = 0;

    // Trace known system objects
    // TODO
    RogueVMList_trace( THIS->global_commands );
    RogueVMList_trace( THIS->type_list );

    // Keep or destroy each object in the master list
    RogueAllocation* cur = THIS->vm_objects;
    THIS->vm_objects = 0;
    while (cur)
    {
      RogueAllocation* next = cur->next_allocation;
      if (cur->size < 0)
      {
        // Keep
        cur->size = ~cur->size;
        cur->next_allocation = survivors;
        survivors = cur;
      }
      else
      {
printf( "Freeing system object of size %d\n", cur->size );
        RogueAllocator_free( &THIS->allocator, cur );
      }
      cur = next;
    }

    THIS->vm_objects = survivors;
  }
}

RogueString* RogueVM_consolidate_string( RogueVM* THIS, RogueString* st )
{
  RogueTableEntry* entry = RogueTable_find_characters( THIS->consolidation_table, st->hash_code,
      st->characters, st->count, 1 );
  entry->value = entry->key;
  return entry->value;
}

RogueString* RogueVM_consolidate_characters( RogueVM* THIS, RogueCharacter* characters, RogueInteger count )
{
  RogueInteger hash_code = RogueString_calculate_hash_code_for_characters( characters, count );
  RogueTableEntry* entry = RogueTable_find_characters( THIS->consolidation_table, hash_code, characters, count, 1 );
  entry->value = entry->key;
  return entry->value;
}

RogueString* RogueVM_consolidate_c_string( RogueVM* THIS, const char* utf8 )
{
  RogueTableEntry* entry = RogueTable_find_c_string( THIS->consolidation_table, utf8, 1 );
  entry->value = entry->key;
  return entry->value;
}

RogueType* RogueVM_create_type( RogueVM* THIS, RogueCmd* origin, const char* name, RogueInteger object_size )
{
  RogueType* type = RogueType_create( THIS, origin, name, object_size );
  RogueVMList_add( THIS->type_list, type );
  RogueVMTable_set( THIS->type_lookup, type->name, type );
  return type;
}

RogueString* RogueVM_error_string( RogueVM* THIS )
{
  RogueString* result;
  const char* bar;

  RogueStringBuilder buffer;
  RogueStringBuilder_init( &buffer, THIS, -1 );

  bar = "===============================================================================\n";
  RogueStringBuilder_print_c_string( &buffer, bar );
  RogueStringBuilder_print_c_string( &buffer, "ERROR" );
  if (THIS->error_filepath)
  {
    RogueStringBuilder_print_c_string( &buffer, " in " );
    RogueStringBuilder_print_object( &buffer, THIS->error_filepath );
    if (THIS->error_position.line)
    {
      RogueStringBuilder_print_c_string( &buffer, " line " );
      RogueStringBuilder_print_integer( &buffer, THIS->error_position.line );
      RogueStringBuilder_print_c_string( &buffer, ", column " );
      RogueStringBuilder_print_integer( &buffer, THIS->error_position.column );
    }
    RogueStringBuilder_print_character( &buffer, ':' );
  }
  RogueStringBuilder_print_c_string( &buffer, "\n" );

  RogueStringBuilder_print_characters( &buffer, THIS->error_message_builder.characters,
      THIS->error_message_builder.count );
  RogueStringBuilder_print_character( &buffer, '\n' );
  RogueStringBuilder_print_c_string( &buffer, bar );

  result = RogueStringBuilder_to_string( &buffer );

  RogueStringBuilder_retire( &buffer );

  return result;
}

void RogueVM_log_error( RogueVM* THIS )
{
  RogueString_log( RogueVM_error_string(THIS) );
}

RogueLogical RogueVM_load_file( RogueVM* THIS, const char* filepath )
{
  RogueLogical success = 1;
  RogueParser* parser;

  ROGUE_TRY(THIS)
  {
    parser = RogueParser_create_with_filepath( THIS, filepath );
    RogueParser_parse_elements( parser );
  }
  ROGUE_CATCH(THIS)
  {
    RogueVM_log_error( THIS );
    success = 0;
  }
  ROGUE_END_TRY(THIS)

  RogueParser_delete( parser );
  return success;
}

