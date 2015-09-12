//=============================================================================
//  RogueVMArray.h
//
//  2015.09.11 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_VM_ARRAY_H
#define ROGUE_VM_ARRAY_H

struct RogueVMArray
{
  RogueAllocation allocation;
  RogueInteger    element_size;
  RogueInteger    count;
  union
  {
    RogueByte      bytes[0];
    RogueCharacter characters[0];
    void*          objects[0];
  };
};

RogueVMArray* RogueVMArray_create( RogueVM* vm, RogueInteger element_size, RogueInteger count );

#endif // ROGUE_VM_ARRAY_H
