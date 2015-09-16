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
  RogueVM*           vm;
  RogueString*       filepath;
  RogueInteger       position;
  RogueInteger       count;
  RogueVMArray*      data;
};

RogueETCReader* RogueETCReader_create_with_file( RogueVM* vm, RogueString* filepath );

RogueLogical    RogueETCReader_has_another( RogueETCReader* THIS );
void            RogueETCReader_load( RogueETCReader* THIS );
RogueInteger    RogueETCReader_read_byte( RogueETCReader* THIS );
RogueInteger    RogueETCReader_read_integer_x( RogueETCReader* THIS );
RogueString*    RogueETCReader_read_string( RogueETCReader* THIS );

RogueCmdList*   RogueETCReader_load_statement_list( RogueETCReader* THIS );
void*           RogueETCReader_load_statement( RogueETCReader* THIS );
void*           RogueETCReader_load_expression( RogueETCReader* THIS );

void            RogueETCReader_throw_type_error( RogueETCReader* THIS, RogueType* type, RogueCmdType opcode );

#endif // ROGUE_READER_H
