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

  THIS->type_ByteArray      = RogueTypeByteArray_create( THIS );
  THIS->type_ByteList       = RogueTypeByteList_create( THIS );
  THIS->type_CharacterArray = RogueTypeCharacterArray_create( THIS );
  THIS->type_ObjectArray    = RogueTypeObjectArray_create( THIS );
  THIS->type_ObjectList     = RogueTypeObjectList_create( THIS );
  THIS->type_ParseReader    = RogueTypeParseReader_create( THIS );
  THIS->type_String         = RogueTypeString_create( THIS );
  THIS->type_Table          = RogueTypeTable_create( THIS );
  THIS->type_TableEntry     = RogueTypeTableEntry_create( THIS );
  THIS->type_Tokenizer      = RogueTypeTokenizer_create( THIS );

  THIS->c_string_buffer = RogueByteList_create( THIS, 200 );
  THIS->global_commands = RogueObjectList_create( THIS, 20 );
  THIS->consolidation_table = RogueTable_create( THIS, 128 );

  THIS->cmd_type_eol = RogueCmdType_create( THIS, ROGUE_TOKEN_EOL, -1 );

  return THIS;
}

RogueVM* RogueVM_delete( RogueVM* THIS )
{
  if (THIS)
  {
    RogueType_delete( THIS->type_ByteArray );
    RogueType_delete( THIS->type_ByteList );
    RogueType_delete( THIS->type_CharacterArray );
    RogueType_delete( THIS->type_ObjectArray );
    RogueType_delete( THIS->type_ObjectList );
    RogueType_delete( THIS->type_ParseReader );
    RogueType_delete( THIS->type_String );
    RogueType_delete( THIS->type_Table );
    RogueType_delete( THIS->type_TableEntry );
    RogueType_delete( THIS->type_Tokenizer );

    RogueAllocator_retire( &THIS->allocator );
    free( THIS );
  }
  return 0;
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

