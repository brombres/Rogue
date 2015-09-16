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
  THIS->type_lookup = RogueVMTable_create( THIS, 64, RogueVMTraceType );

  THIS->type_ByteArray      = RogueTypeByteArray_create( THIS );
  THIS->type_ByteList       = RogueTypeByteList_create( THIS );
  THIS->type_CharacterArray = RogueTypeCharacterArray_create( THIS );
  THIS->type_Integer        = RogueVM_create_type( THIS, 0, "Integer", sizeof(RogueInteger) );
  THIS->type_ObjectArray    = RogueTypeObjectArray_create( THIS );
  THIS->type_ObjectList     = RogueTypeObjectList_create( THIS );
  THIS->type_String         = RogueTypeString_create( THIS );
  THIS->type_Table          = RogueTypeTable_create( THIS );
  THIS->type_TableEntry     = RogueTypeTableEntry_create( THIS );

  THIS->c_string_buffer = RogueByteList_create( THIS, 200 );
  THIS->immediate_commands = RogueCmdList_create( THIS, 20 );
  THIS->consolidation_table = RogueTable_create( THIS, 128 );

  return THIS;
}

RogueVM* RogueVM_delete( RogueVM* THIS )
{
  if (THIS)
  {
    RogueAllocator_retire( &THIS->allocator );
    free( THIS );
  }
  return 0;
}

void RogueVM_collect_garbage( RogueVM* THIS )
{
  // GC system objects
  if (THIS->have_new_vm_objects)
  {
    RogueAllocation* survivors = 0;
    THIS->have_new_vm_objects = 0;

    // Trace known system objects
    RogueCmd_trace( THIS->immediate_commands );
    RogueVMList_trace( THIS->type_list );
    RogueVMTable_trace( THIS->type_lookup );

    // Keep or free each object in the master list
    RogueAllocation* cur = THIS->vm_objects;
    THIS->vm_objects = 0;
    while (cur)
    {
      RogueAllocation* next = cur->next_allocation;
      if (cur->size < 0)
      {
        // Keep
        cur->size ^= -1;
        cur->next_allocation = survivors;
        survivors = cur;
      }
      else
      {
        RogueAllocator_free( &THIS->allocator, cur );
      }
      cur = next;
    }

    THIS->vm_objects = survivors;
  }

  // GC runtime objects
  RogueAllocation* survivors = 0;

  // Trace known objects
  RogueObject_trace( THIS->consolidation_table );
  RogueObject_trace( THIS->c_string_buffer );

  // Keep or free each object in the master list
  RogueAllocation* cur = THIS->objects;
  THIS->objects = 0;
  while (cur)
  {
    RogueAllocation* next = cur->next_allocation;
    if (cur->size < 0)
    {
      // Keep
      cur->size ^= -1;
      cur->next_allocation = survivors;
      survivors = cur;
    }
    else
    {
      //printf( "Freeing runtime object of type %s\n", ((RogueObject*)cur)->type->name );
      //RogueObject_println( cur );
      RogueAllocator_free( &THIS->allocator, cur );
    }
    cur = next;
  }

  THIS->objects = survivors;
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
  RogueInteger line = 0;
  RogueInteger column = 0;

  RogueStringBuilder buffer;
  RogueStringBuilder_init( &buffer, THIS, -1 );

  bar = "===============================================================================\n";
  RogueStringBuilder_print_c_string( &buffer, bar );
  RogueStringBuilder_print_c_string( &buffer, "ERROR" );
  if (THIS->error_filepath)
  {
    RogueStringBuilder_print_c_string( &buffer, " in " );
    RogueStringBuilder_print_object( &buffer, THIS->error_filepath );
    if (line)
    {
      RogueStringBuilder_print_c_string( &buffer, " line " );
      RogueStringBuilder_print_integer( &buffer, line );
      RogueStringBuilder_print_c_string( &buffer, ", column " );
      RogueStringBuilder_print_integer( &buffer, column );
    }
    RogueStringBuilder_print_character( &buffer, ':' );
  }
  RogueStringBuilder_print_c_string( &buffer, "\n" );

  RogueStringBuilder_print_characters( &buffer, THIS->error_message_builder.characters,
      THIS->error_message_builder.count );
  RogueStringBuilder_print_character( &buffer, '\n' );
  RogueStringBuilder_print_c_string( &buffer, bar );

  result = RogueStringBuilder_to_string( &buffer );

  return result;
}

void RogueVM_log_error( RogueVM* THIS )
{
  RogueString_log( RogueVM_error_string(THIS) );
}

RogueLogical RogueVM_load_file( RogueVM* THIS, const char* filepath )
{
  RogueLogical success;

  ROGUE_TRY(THIS)
  {
    RogueETCReader* reader = RogueETCReader_create_with_file( THIS, ROGUE_STRING(THIS,"../RC2/Test.etc") );
    RogueETCReader_load( reader );

    printf( "-------------------------------------------------------------------------------\n" );
    RogueCmd_execute( THIS->immediate_commands );
    printf( "-------------------------------------------------------------------------------\n" );

    THIS->immediate_commands->statements->count = 0;

    success = 1;
  }
  ROGUE_CATCH(THIS)
  {
    RogueVM_log_error( THIS );
    success = 0;
  }
  ROGUE_END_TRY(THIS)

  return success;
}

