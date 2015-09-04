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
RogueType* RogueType_create( RogueVM* vm, const char* name, RogueInteger object_size )
{
  int len = strlen( name );

  RogueType* THIS = (RogueType*) malloc( sizeof(RogueType) );
  memset( THIS, 0, sizeof(RogueType) );
  THIS->vm = vm;

  THIS->name = (char*) malloc( len+1 );
  strcpy( THIS->name, name );

  THIS->object_size = object_size;

  THIS->equals = RogueEqualsFn_default;
  THIS->equals_c_string = RogueEqualsCStringFn_default;
  THIS->equals_characters = RogueEqualsCharactersFn_default;
  THIS->hash_code = RogueHashCodeFn_default;
  THIS->print = RoguePrintFn_default;

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

void* RogueType_create_object( RogueType* THIS, RogueInteger size )
{
  RogueObject* object;

  if (size == -1) size = THIS->object_size;
  object = (RogueObject*) RogueAllocator_allocate( &THIS->vm->allocator, size );
  memset( object, 0, size );

  object->allocation.size = size;
  object->allocation.reference_count = 0;
  object->allocation.next_allocation = (RogueAllocation*) THIS->vm->objects;
  THIS->vm->objects = object;
  object->type = THIS;

  return object;
}

