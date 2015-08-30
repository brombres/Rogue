//=============================================================================
//  Rogue.c
//
//  2015.08.29 by Abe Pralle
//=============================================================================
#include "Rogue.h"


//-----------------------------------------------------------------------------
//  Command Type Info
//-----------------------------------------------------------------------------
RogueCmdType RogueCmdType_add_integer =
{
  RogueExecuteNil_default,
  RogueExecuteInteger_add
};

RogueCmdType RogueCmdType_literal_integer =
{
  RogueExecuteNil_default,
  RogueExecuteInteger_literal_integer
};


//-----------------------------------------------------------------------------
//  Command Functions
//-----------------------------------------------------------------------------
RogueInteger RogueExecuteNil_default( RogueCmd* THIS )
{
  return 0;
}

RogueInteger RogueExecuteInteger_default( RogueCmd* THIS )
{
  return 0;
}

RogueInteger RogueExecuteInteger_add( RogueCmd* THIS )
{
  return THIS->left->type->execute_integer(THIS->left) + THIS->right->type->execute_integer(THIS->right);
}

RogueInteger RogueExecuteInteger_literal_integer( RogueCmd* THIS )
{
  return THIS->integer_value;
}

RogueCmd* RogueCmd_create( RogueCmdType* type )
{
  RogueCmd* result = (RogueCmd*) malloc( sizeof(RogueCmd) );
  memset( result, 0, sizeof(RogueCmd) );
  result->type = type;
  return result;
}

RogueCmd* RogueCmd_create_binary( RogueCmdType* type, RogueCmd* left, RogueCmd* right )
{
  RogueCmd* result = RogueCmd_create( type );
  result->left = left;
  result->right = right;
  return result;
}

RogueCmd* RogueCmd_create_literal_integer( RogueInteger value )
{
  RogueCmd* result = RogueCmd_create( &RogueCmdType_literal_integer );
  result->integer_value = value;
  return result;
}
