//=============================================================================
//  RogueType.c
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#include "Rogue.h"

//-----------------------------------------------------------------------------
//  Dynamic Functions
//-----------------------------------------------------------------------------
int RogueEqualsFn_default( void* object_a, void* object_b )
{
  return (object_a == object_b);
}

int RogueEqualsCStringFn_default( void* object_a, const char* b )
{
  return 0;
}

int RogueEqualsCharactersFn_default( void* object_a, RogueInteger hash_code,
    RogueCharacter* characters, RogueInteger count )
{
  return 0;
}

RogueInteger RogueHashCodeFn_default( void* object )
{
  return (RogueInteger)(intptr_t)object;
}

void RoguePrintFn_default( void* object, RogueStringBuilder* builder )
{
  RogueStringBuilder_print_character( builder, '(' );
  RogueStringBuilder_print_c_string( builder, ((RogueObject*)object)->type->name );
  RogueStringBuilder_print_character( builder, ')' );
}


//-----------------------------------------------------------------------------
//  RogueType
//-----------------------------------------------------------------------------
RogueType* RogueType_create( RogueVM* vm, RogueCmd* origin, const char* name, RogueInteger object_size )
{
  int len = strlen( name );

  RogueType* THIS = (RogueType*) malloc( sizeof(RogueType) );
  memset( THIS, 0, sizeof(RogueType) );
  THIS->vm = vm;
  THIS->origin = origin;

  THIS->name = (char*) malloc( len+1 );
  strcpy( THIS->name, name );

  THIS->object_size = object_size;

  THIS->equals = RogueEqualsFn_default;
  THIS->equals_c_string = RogueEqualsCStringFn_default;
  THIS->equals_characters = RogueEqualsCharactersFn_default;
  THIS->hash_code = RogueHashCodeFn_default;
  THIS->print = RoguePrintFn_default;

  THIS->methods = RogueVMList_create( vm, 20 );

  return THIS;
}

RogueType* RogueType_delete( RogueType* THIS )
{
  if (THIS)
  {
    free( THIS->name );
    free( THIS );
  }
  return 0;
}

