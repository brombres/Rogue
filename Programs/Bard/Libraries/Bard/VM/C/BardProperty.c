#include <stdlib.h>

#include "Bard.h"

BardProperty* BardProperty_create( char* name, BardType* type )
{
  BardProperty* property = CROM_SYSTEM_MALLOC( sizeof(BardProperty) );
  property->name = name;
  property->type = type;
  return property;
}

void BardProperty_destroy( BardProperty* p )
{
  CROM_SYSTEM_FREE( p );
}

int BardProperty_determine_offset( BardProperty* property, int offset )
{
  int size = property->type->reference_size;
  if (size >= 2)
  {
    if (offset & 1) ++offset;
    if (size >= 4)
    {
      while (offset & 3) ++offset;
      if (size >= 8)
      {
        while (offset & 7) ++offset;
      }
    }
  }
  property->offset = offset;
  offset += size;
  return offset;
}

