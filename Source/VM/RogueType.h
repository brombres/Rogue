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
typedef int          (*RogueEqualsFn)( void* object_a, void* object_b );
typedef int          (*RogueEqualsCharactersFn)( void* object_a,
                       RogueInteger hash_code, RogueCharacter* characters, RogueInteger count );
typedef int          (*RogueEqualsCStringFn)( void* object_a, const char* b );
typedef RogueInteger (*RogueHashCodeFn)( void* object );
typedef void         (*RoguePrintFn)( void* object, RogueStringBuilder* builder );
typedef void         (*RogueTraceFn)( void* object );

int          RogueEqualsFn_default( void* object_a, void* object_b );
int          RogueEqualsCharactersFn_default( void* object_a, RogueInteger, RogueCharacter*, RogueInteger );
int          RogueEqualsCStringFn_default( void* object_a, const char* b );
RogueInteger RogueHashCodeFn_default( void* object );
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

  RogueEqualsFn           equals;
  RogueEqualsCStringFn    equals_c_string;
  RogueEqualsCharactersFn equals_characters;
  RogueHashCodeFn         hash_code;
  RoguePrintFn            print;
  RogueTraceFn            trace;

  RogueVMList*            methods;
};

RogueType* RogueType_create( RogueVM* vm, RogueCmd* origin, const char* name, RogueInteger object_size );
RogueType* RogueType_delete( RogueType* THIS );

#endif // ROGUE_TYPE_H
