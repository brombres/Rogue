//=============================================================================
//  RogueCmd.h
//
//  2015.08.29 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_CMD_H
#define ROGUE_CMD_H

#include "Rogue.h"

enum RogueCmdID
{
  ROGUE_CMD_UNDEFINED,
  ROGUE_CMD_EOL,
  ROGUE_CMD_LITERAL_INTEGER,
  ROGUE_CMD_SYMBOL_CLOSE_PAREN,
  ROGUE_CMD_SYMBOL_EQ,
  ROGUE_CMD_SYMBOL_EQUALS,
  ROGUE_CMD_SYMBOL_EXCLAMATION,
  ROGUE_CMD_SYMBOL_GE,
  ROGUE_CMD_SYMBOL_GT,
  ROGUE_CMD_SYMBOL_LE,
  ROGUE_CMD_SYMBOL_LT,
  ROGUE_CMD_SYMBOL_NE,
  ROGUE_CMD_SYMBOL_OPEN_PAREN,
  ROGUE_CMD_SYMBOL_PLUS,
  ROGUE_CMD_SYMBOL_POUND,
  ROGUE_CMD_STATEMENT_LIST,
};

//-----------------------------------------------------------------------------
//  RogueCmdType
//-----------------------------------------------------------------------------
struct RogueCmdType
{
  RogueAllocation allocation;
  RogueVM*        vm;
  RogueCmdID      id;
  RogueInteger    object_size;
  const char*     name;
};

RogueCmdType* RogueCmdType_create( RogueVM* vm, RogueCmdID id,
    const char* name, RogueInteger object_size );

//-----------------------------------------------------------------------------
//  RogueCmd
//-----------------------------------------------------------------------------
struct RogueCmd
{
  RogueAllocation allocation;
  RogueCmdType*   type;
};

struct RogueCmdStatementList
{
  RogueCmd     cmd;
  RogueVMList* statements;
};

void RogueCmdStatementList_init( void* cmd );

struct RogueCmdLiteralInteger
{
  RogueCmd     cmd;
  RogueInteger value;
};

struct RogueCmdUnaryOp
{
  RogueCmd  cmd;
  RogueCmd* operand;
};

struct RogueCmdBinaryOp
{
  RogueCmd  cmd;
  RogueCmd* left;
  RogueCmd* right;
};

void*         RogueCmd_create( RogueCmdType* of_type );
void          RogueCmd_init( void* THIS );
void          RogueCmd_print( void* THIS, RogueStringBuilder* builder );
void          RogueCmd_throw_error( RogueCmd* THIS, const char* message );
void          RogueCmd_trace( void* THIS );
RogueCmdType* RogueCmd_type( void* THIS );

#endif // ROGUE_CMD_H
