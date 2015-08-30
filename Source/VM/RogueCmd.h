//=============================================================================
//  RogueCmd.h
//
//  2015.08.29 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_CMD_H
#define ROGUE_CMD_H

#include "Rogue.h"

typedef struct RogueCmd RogueCmd;

//-----------------------------------------------------------------------------
//  Command Functions
//-----------------------------------------------------------------------------
typedef RogueInteger (*RogueExecuteNilFn)( RogueCmd* THIS );
typedef RogueInteger (*RogueExecuteIntegerFn)( RogueCmd* THIS );

RogueInteger RogueExecuteNil_default( RogueCmd* THIS );

RogueInteger RogueExecuteInteger_default( RogueCmd* THIS );
RogueInteger RogueExecuteInteger_add( RogueCmd* THIS );
RogueInteger RogueExecuteInteger_literal_integer( RogueCmd* THIS );


//-----------------------------------------------------------------------------
//  Command Type Info
//-----------------------------------------------------------------------------
typedef struct RogueCmdType
{
  RogueExecuteNilFn     execute_nil;
  RogueExecuteIntegerFn execute_integer;
} RogueCmdType;

extern RogueCmdType RogueCmdType_add_integer;
extern RogueCmdType RogueCmdType_literal_integer;


//-----------------------------------------------------------------------------
//  Commands
//-----------------------------------------------------------------------------
struct RogueCmd
{
  RogueCmdType* type;

  union
  {
    RogueCmd*    operand;

    struct
    {
      RogueCmd* left;
      RogueCmd* right;
    };

    RogueInteger integer_value;
  };
};

RogueCmd* RogueCmd_create( RogueCmdType* type );
RogueCmd* RogueCmd_create_binary( RogueCmdType* type, RogueCmd* left, RogueCmd* right );
RogueCmd* RogueCmd_create_literal_integer( RogueInteger value );

#endif // ROGUE_CMD_H
