//=============================================================================
//  RogueObject.h
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_OBJECT_H
#define ROGUE_OBJECT_H

struct RogueObject
{
  RogueAllocation allocation;
  RogueType*      type;
};

RogueObject* RogueObject_create( RogueType* type, RogueInteger size );

#endif // ROGUE_OBJECT_H
