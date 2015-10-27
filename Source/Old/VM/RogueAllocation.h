//=============================================================================
//  RogueAllocation.h
//
//  2015.09.05 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_ALLOCATION_H
#define ROGUE_ALLOCATION_H

#include "Rogue.h"

//-----------------------------------------------------------------------------
//  RogueAllocation
//-----------------------------------------------------------------------------
struct RogueAllocation
{
  // PROPERTIES
  RogueInteger     size;
  RogueInteger     reference_count;
  RogueAllocation* next_allocation;
};

#endif // ROGUE_ALLOCATION_H
