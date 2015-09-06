//=============================================================================
//  RogueCmd.h
//
//  2015.08.29 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_CMD_H
#define ROGUE_CMD_H

#include "Rogue.h"

typedef void (*RogueCmdPrintFn)( void* cmd );

//-----------------------------------------------------------------------------
//  RogueCmdType
//-----------------------------------------------------------------------------
struct RogueCmdType
{
  RogueVM*        vm;
  RogueTokenType  token_type;
  RogueInteger    object_size;
  const char*     name;
  RogueCmdPrintFn print;
};

RogueCmdType* RogueCmdType_create( RogueVM* vm, RogueTokenType token_type,
    const char* name, RogueInteger object_size );

void RogueCmdLiteralInteger_print( void* cmd );

//-----------------------------------------------------------------------------
//  RogueCmd
//-----------------------------------------------------------------------------
struct RogueCmd
{
  RogueAllocation allocation;
  RogueCmdType*   type;
};

struct RogueCmdLiteralInteger
{
  RogueCmd     cmd;
  RogueInteger value;
};

struct RogueCmdBinaryOp
{
  RogueCmd cmd;
  void*    left;
  void*    right;
};

void* RogueCmd_create( RogueCmdType* of_type );

#endif // ROGUE_CMD_H
