#ifndef BARD_PROPERTY_H
#define BARD_PROPERTY_H

#include "BardXC.h"

typedef struct BardProperty
{
  char*     name;     // Does not need to be freed - comes from VM's session id_list.
  BardType* type;
  int       offset;   // Byte offset from beginning of object.
} BardProperty;

BardProperty* BardProperty_create( char* name, BardType* type );
void          BardProperty_destroy( BardProperty* p );

int BardProperty_determine_offset( BardProperty* property, int unaligned_offset );

#endif // BARD_PROPERTY_H
