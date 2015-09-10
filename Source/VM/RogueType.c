//=============================================================================
//  RogueType.c
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#include "Rogue.h"

//-----------------------------------------------------------------------------
//  Dynamic Functions
//-----------------------------------------------------------------------------
RogueInteger RogueIntrinsicFn_default( RogueIntrinsicFnType fn_type,
    RogueObject* context, void* parameter )
{
  switch (fn_type)
  {
    case ROGUE_INTRINSIC_FN_TRACE:
      break;

    case ROGUE_INTRINSIC_FN_HASH_CODE:
      return (RogueInteger)(intptr_t)context;

    case ROGUE_INTRINSIC_FN_TO_STRING:
    {
      RogueStringBuilder* builder = (RogueStringBuilder*) parameter;
      RogueStringBuilder_print_character( builder, '(' );
      RogueStringBuilder_print_c_string( builder, context->type->name );
      RogueStringBuilder_print_character( builder, ')' );
      return 0;
    }

    case ROGUE_INTRINSIC_FN_EQUALS_OBJECT:
      return (context == parameter);

    case ROGUE_INTRINSIC_FN_EQUALS_C_STRING:
      return 0;

    case ROGUE_INTRINSIC_FN_EQUALS_CHARACTERS:
      return 0;
  }
  return 0;
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

  THIS->intrinsic_fn = RogueIntrinsicFn_default;

  THIS->methods = RogueVMList_create( vm, 20, RogueVMTraceMethod );

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

