//=============================================================================
//  RogueType.h
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_TYPE_H
#define ROGUE_TYPE_H

enum RogueIntrinsicFnType
{
  ROGUE_INTRINSIC_FN_TRACE,
  ROGUE_INTRINSIC_FN_HASH_CODE,
  ROGUE_INTRINSIC_FN_OBJECT_EQUALS_OBJECT,
  ROGUE_INTRINSIC_FN_OBJECT_EQUALS_C_STRING,
  ROGUE_INTRINSIC_FN_OBJECT_EQUALS_CHARACTERS,
};

//-----------------------------------------------------------------------------
//  Dynamic Functions
//-----------------------------------------------------------------------------
typedef RogueInteger (*RogueIntrinsicFn)( RogueIntrinsicFnType fn_type, RogueObject* context, void* parameter );

RogueInteger RogueIntrinsicFn_default( RogueIntrinsicFnType fn_type, RogueObject* context, void* parameter );

typedef int          (*RogueEqualsCharactersFn)( void* object_a,
                       RogueInteger hash_code, RogueCharacter* characters, RogueInteger count );
typedef int          (*RogueEqualsCStringFn)( void* object_a, const char* b );
typedef RogueInteger (*RogueHashCodeFn)( void* object );
typedef void         (*RoguePrintFn)( void* object, RogueStringBuilder* builder );
typedef void         (*RogueTraceFn)( void* object );

int          RogueEqualsCharactersFn_default( void* object_a, RogueInteger, RogueCharacter*, RogueInteger );
void         RoguePrintFn_default( void* object, RogueStringBuilder* builder );

//-----------------------------------------------------------------------------
//  RogueType
//-----------------------------------------------------------------------------
struct RogueType
{
  RogueVM*      vm;
  RogueCmd*     origin;
  char*         name;
  RogueString*  name_object;
  RogueInteger  object_size;
  RogueInteger  element_size;

  RogueIntrinsicFn        intrinsic_fn;
  RoguePrintFn            print;
  RogueTraceFn            trace;

  RogueVMList*            methods;
};

RogueType* RogueType_create( RogueVM* vm, RogueCmd* origin, const char* name, RogueInteger object_size );
RogueType* RogueType_delete( RogueType* THIS );

#endif // ROGUE_TYPE_H
