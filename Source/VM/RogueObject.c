//=============================================================================
//  RogueObject.c
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#include "Rogue.h"

void* RogueObject_create( RogueType* of_type, RogueInteger size )
{
  RogueObject* object;
  RogueVM*     vm = of_type->vm;

  if (size == -1) size = of_type->object_size;

  object = RogueAllocator_allocate( &vm->allocator, size );

  object->type = of_type;
  object->allocation.next_allocation = (RogueAllocation*) vm->objects;
  vm->objects = object;

  return object;
}

RogueLogical RogueObject_equals_c_string( void* THIS, const char* c_string )
{
  return ((RogueObject*)THIS)->type->intrinsic_fn(
      ROGUE_INTRINSIC_FN_EQUALS_C_STRING,
      (RogueObject*)THIS,
      (void*)c_string
    );
}

RogueLogical RogueObject_equals_characters( void* THIS, RogueCharacter* characters,
                                            RogueInteger count, RogueInteger hash_code )
{
  RogueCharacterInfo info;
  info.characters = characters;
  info.count = count;
  info.hash_code = hash_code;

  return ((RogueObject*)THIS)->type->intrinsic_fn(
      ROGUE_INTRINSIC_FN_EQUALS_CHARACTERS,
      (RogueObject*)THIS,
      &info
    );
}

RogueInteger RogueObject_hash_code( void* THIS )
{
  return ((RogueObject*)THIS)->type->intrinsic_fn( ROGUE_INTRINSIC_FN_HASH_CODE, (RogueObject*)THIS, 0 );
}

void RogueObject_print( void* THIS )
{
  RogueStringBuilder builder;
  RogueStringBuilder_init( &builder, ((RogueObject*)THIS)->type->vm, -1 );

  RogueObject_to_string( THIS, &builder );

  RogueStringBuilder_log( &builder );
}

void RogueObject_println( void* THIS )
{
  RogueObject_print( THIS );
  printf( "\n" );
}

void RogueObject_to_string( void* THIS, RogueStringBuilder* builder )
{
  ((RogueObject*)THIS)->type->intrinsic_fn( ROGUE_INTRINSIC_FN_TO_STRING, (RogueObject*)THIS, builder );
}
