//=============================================================================
//  RogueETCReader.h
//
//  2015.09.03 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_READER_H
#define ROGUE_READER_H

//-----------------------------------------------------------------------------
//  ETCReader Type
//-----------------------------------------------------------------------------
RogueType* RogueTypeETCReader_create( RogueVM* vm );

//-----------------------------------------------------------------------------
//  ETCReader Object
//-----------------------------------------------------------------------------
struct RogueETCReader
{
  RogueObject        object;
  RogueString*       filepath;
  RogueInteger       position;
  RogueInteger       count;
  RogueArray*        data;
};

RogueETCReader* RogueETCReader_create( RogueVM* vm, RogueString* filepath );

RogueLogical   RogueETCReader_consume( RogueETCReader* THIS, RogueCharacter ch );
RogueLogical   RogueETCReader_consume_whitespace( RogueETCReader* THIS );
RogueLogical   RogueETCReader_has_another( RogueETCReader* THIS );
RogueLogical   RogueETCReader_next_is_digit( RogueETCReader* THIS, RogueInteger base );
RogueCharacter RogueETCReader_peek( RogueETCReader* THIS, RogueInteger lookahead );
RogueCharacter RogueETCReader_read( RogueETCReader* THIS );
RogueInteger   RogueETCReader_read_digit( RogueETCReader* THIS );

#endif // ROGUE_READER_H
