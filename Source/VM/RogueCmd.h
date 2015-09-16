//=============================================================================
//  RogueCmd.h
//
//  2015.08.29 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_CMD_H
#define ROGUE_CMD_H

#include "Rogue.h"

enum RogueCmdType
{
  // ETC commands
  ROGUE_CMD_LIST,
  ROGUE_CMD_LOG,
  ROGUE_CMD_LOG_VALUE,
  ROGUE_CMD_LITERAL_INTEGER,
  ROGUE_CMD_LITERAL_STRING,
  ROGUE_CMD_ADD,

  // VM commands
  ROGUE_CMD_LOG_INTEGER,
  ROGUE_CMD_LOG_STRING,
};

//-----------------------------------------------------------------------------
//  RogueCmd
//-----------------------------------------------------------------------------
struct RogueCmd
{
  RogueAllocation allocation;
  RogueVM*        vm;
  RogueCmdType    cmd_type;
};

struct RogueCmdList
{
  RogueCmd     cmd;
  RogueVMList* statements;
};

RogueCmdList* RogueCmdList_create( RogueVM* vm, RogueInteger initial_capacity );

struct RogueCmdLiteralInteger
{
  RogueCmd     cmd;
  RogueInteger value;
};

struct RogueCmdLiteralString
{
  RogueCmd     cmd;
  RogueString* value;
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

void*                   RogueCmd_create( RogueVM* vm, RogueCmdType cmd_type, size_t object_size );
RogueCmdBinaryOp*       RogueCmdBinaryOp_create( RogueVM* vm, RogueCmdType cmd_type, RogueCmd* left, RogueCmd* right );
RogueCmdLiteralInteger* RogueCmdLiteralInteger_create( RogueVM* vm, RogueInteger value );
RogueCmdLiteralString*  RogueCmdLiteralString_create( RogueVM* vm, RogueString* value );
RogueCmdUnaryOp*        RogueCmdUnaryOp_create( RogueVM* vm, RogueCmdType cmd_type, RogueCmd* operand );
void                    RogueCmd_print( void* THIS, RogueStringBuilder* builder );
void                    RogueCmd_throw_error( RogueCmd* THIS, const char* message );
void                    RogueCmd_trace( void* THIS );
RogueType*              RogueCmd_type( void* THIS );

void*        RogueCmd_resolve( void* THIS );

void         RogueCmd_execute( void* THIS );
RogueInteger RogueCmd_execute_integer_op( void* THIS );
RogueString* RogueCmd_execute_string_op( void* THIS );

#endif // ROGUE_CMD_H
