//=============================================================================
//  RogueType.h
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_TYPE_H
#define ROGUE_TYPE_H

//-----------------------------------------------------------------------------
//  RogueType
//-----------------------------------------------------------------------------
struct RogueType
{
  RogueVM*     vm;
  RogueString* name;
  RogueInteger object_size;
};

RogueType* RogueType_create( RogueVM* vm );

#endif // ROGUE_TYPE_H
