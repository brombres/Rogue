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

void RogueObject_print( void* THIS );
void RogueObject_println( void* THIS );

#endif // ROGUE_OBJECT_H
