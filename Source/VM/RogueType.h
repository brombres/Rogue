//=============================================================================
//  RogueType.h
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_TYPE_H
#define ROGUE_TYPE_H

//-----------------------------------------------------------------------------
//  Dynamic Functions
//-----------------------------------------------------------------------------
typedef void (*RogueTraceFn)( void* object );
typedef void (*RoguePrintFn)( void* object, RogueStringBuilder* builder );

void RoguePrintFn_default( void* object, RogueStringBuilder* builder );


//-----------------------------------------------------------------------------
//  RogueType
//-----------------------------------------------------------------------------
struct RogueType
{
  RogueVM*      vm;
  char*         name;
  RogueString*  name_object;
  RogueInteger  object_size;
  RogueInteger  element_size;

  RogueTraceFn  trace;
  RoguePrintFn  print;
};

RogueType* RogueType_create( RogueVM* vm, const char* name, RogueInteger object_size );
RogueType* RogueType_delete( RogueType* THIS );

void* RogueType_create_object( RogueType* THIS, RogueInteger size );

#endif // ROGUE_TYPE_H
