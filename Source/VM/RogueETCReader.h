//=============================================================================
//  RogueETCReader.h
//
//  2015.09.03 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_READER_H
#define ROGUE_READER_H

//-----------------------------------------------------------------------------
//  ETCReader
//-----------------------------------------------------------------------------
struct RogueETCReader
{
  RogueAllocation    allocation;
  RogueString*       filepath;
  RogueInteger       position;
  RogueInteger       count;
  RogueVMArray*      data;
};

RogueETCReader* RogueETCReader_create_with_file( RogueVM* vm, RogueString* filepath );

RogueInteger    RogueETCReader_read_byte( RogueETCReader* THIS );
RogueLogical    RogueETCReader_has_another( RogueETCReader* THIS );
RogueInteger    RogueETCReader_read_integer_x( RogueETCReader* THIS );

#endif // ROGUE_READER_H
