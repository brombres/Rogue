//=============================================================================
//  RogueParseReader.c
//
//  2015.09.03 by Abe Pralle
//=============================================================================
#include "Rogue.h"

//-----------------------------------------------------------------------------
//  ParseReader Type
//-----------------------------------------------------------------------------
RogueType* RogueTypeParseReader_create( RogueVM* vm )
{
  RogueType* THIS = RogueType_create( vm, "ParseReader", sizeof(RogueParseReader) );
  return THIS;
}

//-----------------------------------------------------------------------------
//  ParseReader Object
//-----------------------------------------------------------------------------
RogueParseReader* RogueParseReader_create( RogueVM* vm, RogueString* filepath )
{
  RogueParseReader* reader = RogueType_create_object( vm->type_ParseReader, -1 );
  reader->filepath = RogueVM_consolidate_string( vm, filepath );

  //{
    //FILE* fp = fopen( 

  return reader;
}

