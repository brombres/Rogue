//=============================================================================
//  RogueETCLoader.h
//
//  2015.09.03 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_READER_H
#define ROGUE_READER_H

//-----------------------------------------------------------------------------
//  ETCReader
//-----------------------------------------------------------------------------
struct RogueETCLoader
{
  RogueAllocation allocation;
  RogueVM*        vm;
  RogueString*    filepath;
  RogueInteger    position;
  RogueInteger    count;
  RogueVMArray*   data;

  RogueVMList*    type_list;
  RogueVMList*    strings;
};

RogueETCLoader* RogueETCLoader_create_with_file( RogueVM* vm, RogueString* filepath );

RogueLogical    RogueETCLoader_has_another( RogueETCLoader* THIS );
void            RogueETCLoader_load( RogueETCLoader* THIS );
RogueInteger    RogueETCLoader_read_byte( RogueETCLoader* THIS );
RogueInteger    RogueETCLoader_read_integer( RogueETCLoader* THIS );
RogueInteger    RogueETCLoader_read_integer_x( RogueETCLoader* THIS );
RogueReal       RogueETCLoader_read_real( RogueETCLoader* THIS );
RogueString*    RogueETCLoader_read_string( RogueETCLoader* THIS );

RogueCmdList*   RogueETCLoader_load_statement_list( RogueETCLoader* THIS );
void*           RogueETCLoader_load_statement( RogueETCLoader* THIS );
void*           RogueETCLoader_load_expression( RogueETCLoader* THIS );

void            RogueETCLoader_throw_type_error( RogueETCLoader* THIS, RogueType* type, RogueCmdType opcode );

#endif // ROGUE_READER_H
