#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "Bard.h"

//=============================================================================
// BardMM
//=============================================================================
BardMM* BardMM_init( BardMM* mm, BardVM* vm )
{
  memset( mm, 0, sizeof(BardMM) );
  mm->vm = vm;
  return mm;
}

void BardMM_free_data( BardMM* mm )
{
  // Call clean_up() on all objects requiring it.
  BardMM_clean_all( mm );

  BardMM_free_all( mm, &mm->objects );
  BardMM_free_all( mm, &mm->objects_requiring_cleanup );
}

void BardMM_clean_all( BardMM* mm )
{
  BardVM* vm = mm->vm;

  BardObject* cur = mm->objects_requiring_cleanup;
  mm->objects_requiring_cleanup = NULL;

  while (cur)
  {
//printf("Freeing %s\n",cur->type->name);
    BardObject* next = (BardObject*) cur->next_allocation;

    cur->next_allocation = mm->objects;
    mm->objects = cur;

    BardVM_push_object( vm, cur );
    BardVM_invoke( vm, cur->type, cur->type->m_destroy );

    cur = next;
  }
}

void BardMM_free_all( BardMM* mm, BardObject** list )
{
  //Crom* crom = mm->vm->crom;
  BardObject* cur = *list;
  *list = NULL;

  while (cur)
  {
//printf("Freeing %s\n",cur->type->name);
    BardObject* next = (BardObject*) cur->next_allocation;
    Crom_free( crom, cur, cur->size );
    cur = next;
  }
}

BardObject* BardMM_create_object( BardMM* mm, BardType* type, int size )
{
  if (size == -1) size = type->object_size;
  mm->bytes_allocated_since_gc += size;
//printf( "Creating %s of size %d\n", type->name, size );

  BardObject* obj = (BardObject*) Crom_allocate( mm->vm->crom, size );
  obj->type = type;
  obj->size = size;

  if (type->m_destroy)
  {
    obj->next_allocation = mm->objects_requiring_cleanup;
    mm->objects_requiring_cleanup = obj;
  }
  else
  {
    obj->next_allocation = mm->objects;
    mm->objects = obj;
  }
  return obj;
}

