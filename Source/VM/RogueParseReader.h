//=============================================================================
//  RogueParseReader.h
//
//  2015.09.03 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_READER_H
#define ROGUE_READER_H

//-----------------------------------------------------------------------------
//  ParseReader Type
//-----------------------------------------------------------------------------
RogueType* RogueTypeParseReader_create( RogueVM* vm );

//-----------------------------------------------------------------------------
//  ParsePosition
//-----------------------------------------------------------------------------
struct RogueParsePosition
{
  int index;
  int line;
  int column;
};

//-----------------------------------------------------------------------------
//  ParseReader Object
//-----------------------------------------------------------------------------
struct RogueParseReader
{
  RogueObject        object;
  RogueString*       filepath;
  RogueParsePosition position;
  RogueInteger       count;
  RogueArray*        data;
};

RogueParseReader* RogueParseReader_create( RogueVM* vm, RogueString* filepath );

RogueLogical   RogueParseReader_consume( RogueParseReader* THIS, RogueCharacter ch );
RogueLogical   RogueParseReader_consume_whitespace( RogueParseReader* THIS );
RogueLogical   RogueParseReader_has_another( RogueParseReader* THIS );
RogueLogical   RogueParseReader_next_is_digit( RogueParseReader* THIS, RogueInteger base );
RogueCharacter RogueParseReader_peek( RogueParseReader* THIS, RogueInteger lookahead );
RogueCharacter RogueParseReader_read( RogueParseReader* THIS );
RogueInteger   RogueParseReader_read_digit( RogueParseReader* THIS );

#endif // ROGUE_READER_H
