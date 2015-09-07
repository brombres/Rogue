//=============================================================================
//  RogueVMList.h
//
//  2015.09.05 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_VM_LIST_H
#define ROGUE_VM_LIST_H

typedef struct RogueVMArray
{
  RogueAllocation allocation;
  union
  {
    RogueByte bytes[0];
    void*     objects[0];
  };
} RogueVMArray;

struct RogueVMList
{
  RogueAllocation allocation;
  RogueVM*        vm;
  RogueVMArray*   array;
  RogueInteger    capacity;
  RogueInteger    count;
};

RogueVMList* RogueVMList_create( RogueVM* vm, RogueInteger initial_capacity );
RogueVMList* RogueVMList_delete( RogueVMList* THIS );

RogueVMList* RogueVMList_add( RogueVMList* THIS, void* object );
RogueVMList* RogueVMList_reserve( RogueVMList* THIS, RogueInteger additional_capacity );

#endif // ROGUE_VM_LIST_H
