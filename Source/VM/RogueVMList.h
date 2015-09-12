//=============================================================================
//  RogueVMList.h
//
//  2015.09.05 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_VM_LIST_H
#define ROGUE_VM_LIST_H

struct RogueVMList
{
  RogueAllocation allocation;
  RogueVM*        vm;
  RogueVMArray*   array;
  RogueInteger    capacity;
  RogueInteger    count;
  RogueVMTraceFn  trace_fn;
};

RogueVMList* RogueVMList_create( RogueVM* vm, RogueInteger initial_capacity, RogueVMTraceFn trace_fn );

RogueVMList* RogueVMList_add( RogueVMList* THIS, void* object );
RogueVMList* RogueVMList_reserve( RogueVMList* THIS, RogueInteger additional_capacity );

void         RogueVMList_trace( RogueVMList* THIS );

#endif // ROGUE_VM_LIST_H
