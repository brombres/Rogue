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
  RogueAllocator_init( &THIS->allocator );

  THIS->type_ObjectArray = RogueTypeObjectArray_create( THIS );
  THIS->type_String      = RogueTypeString_create( THIS );
  THIS->type_Table       = RogueTypeTable_create( THIS );
  THIS->type_TableEntry  = RogueTypeTableEntry_create( THIS );

  return THIS;
}

RogueVM* RogueVM_delete( RogueVM* THIS )
{
  if (THIS)
  {
    RogueType_delete( THIS->type_ObjectArray );
    RogueType_delete( THIS->type_String );
    RogueType_delete( THIS->type_Table );
    RogueType_delete( THIS->type_TableEntry );

    RogueAllocator_retire( &THIS->allocator );
    free( THIS );
  }
  return 0;
}

RogueString* RogueVM_consolidate( RogueVM* THIS, RogueString* st )
{
  // TODO
  return st;
}

RogueString* RogueVM_consolidate_characters( RogueVM* THIS, RogueCharacter* characters, RogueInteger count )
{
  return RogueVM_consolidate( THIS, RogueString_create_from_characters(THIS, characters, count) );
}

RogueString* RogueVM_consolidate_utf8( RogueVM* THIS, const char* utf8, int utf8_count )
{
  return RogueVM_consolidate( THIS, RogueString_create_from_utf8(THIS, utf8, utf8_count) );
}

