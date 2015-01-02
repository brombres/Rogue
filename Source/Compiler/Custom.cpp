// $[prefix BardXC]

// $[header]
#if defined(_WIN32)
  typedef __int32          BardXCInteger;
  typedef unsigned __int16 BardXCCharacter;
  typedef unsigned char    BardXCByte;
  typedef double           BardXCReal;
  typedef BardXCInteger    BardXCLogical;
  typedef float            BardXCReal32;
  typedef __int64          BardXCInt64;
#else
  typedef int              BardXCInteger;
  typedef unsigned short   BardXCCharacter;
  typedef unsigned char    BardXCByte;
  typedef double           BardXCReal;
  typedef BardXCInteger    BardXCLogical;
  typedef float            BardXCReal32;
  typedef long long        BardXCInt64;
#endif

#include "BardAllocator.h"

//-----------------------------------------------------------------------------
//  CORE TYPES
//-----------------------------------------------------------------------------
struct BardXCType
{
  int          type_info_index;

  int          attributes;
  int          object_size;

  int          base_type_count;
  BardXCType** base_types;

  int          method_count;
  void**       methods;

  int          aspect_count;
  BardXCType** aspect_types;
  void***      aspect_methods;
};

struct BardXCObject
{
  // These two must come first - they mirror the fields in BardAllocator,
  // which is used there to chain free small allocations.
  BardXCObject* next_allocation;  // For tracking an object over its lifetime
  BardXCObject* next_reference;   // For building a linked list of referenced objects during GC

  BardXCType*   type;

  BardXCInteger size;
    // - In bytes; complemented per-object during GC
    // - Size is >= 0 in-between GC's.
    // - During GC, size is set to ~size before tracing through.
    // - After trace, size<0 objects are collected (with size=~size) and
    //   size >= 0 objects are recycled or freed.
};

struct BardXCReferenceArray
{
  BardXCObject  header;
  BardXCInteger  count;
  BardXCObject* data[1];
};

extern int           BardXC_type_count;
extern BardXCType    BardXC_types[];
extern int           BardXC_type_info[];

extern int           BardXC_method_count;
extern void*         BardXC_methods[];

extern BardXCObject* BardXC_objects;
extern BardXCObject* BardXC_objects_requiring_cleanup;

BardXCObject* BardXC_create_object( int type_index, int size=-1 );

void BardXC_init();

void BardXCType_init( BardXCType* type );
bool BardXCType_instance_of( BardXCType* type, int base_type_index );


//=============================================================================
// $[code]
//=============================================================================
#include <string.h>
#include <math.h>

#ifndef NULL
#  define NULL 0
#endif

#define BARDXC_FLAG_REQUIRES_CLEANUP (1<<13)

static double BardXC_pi = acos(-1.0);

static int    BardXC_initialized = false;
BardXCObject* BardXC_objects = NULL;
BardXCObject* BardXC_objects_requiring_cleanup = NULL;

BardXCObject* BardXC_create_object( int type_index, int size )
{
  BardXCType* of_type = &BardXC_types[ type_index ];
  if (size == -1) size = of_type->object_size;

  BardXCObject* obj = (BardXCObject*) BardAllocator_allocate( size );
  obj->type = of_type;
  obj->size = size;
  if (of_type->attributes & BARDXC_FLAG_REQUIRES_CLEANUP)
  {
    obj->next_allocation = BardXC_objects_requiring_cleanup;
    BardXC_objects_requiring_cleanup = obj;
  }
  else
  {
    obj->next_allocation = BardXC_objects;
    BardXC_objects = obj;
  }

  return obj;
}

void BardXC_init()
{
  if (BardXC_initialized) return;
  BardXC_initialized = true;

  // Init types
  for (int i=0; i<BardXC_type_count; ++i)
  {
    BardXCType_init( &BardXC_types[i] );
  }
}


void BardXCType_init( BardXCType* type )
{
  // At program start only type_info_index is initialized.  All other properties
  // are encoded as an integer sequence beginning at that index.
  int* data = (BardXC_type_info + type->type_info_index) - 1;

  int count = *(++data);
  type->base_type_count = count;
  type->base_types = new BardXCType*[ count ];
  for (int i=0; i<count; ++i)
  {
    type->base_types[i] = &BardXC_types[ *(++data) ];
  }

  // Dynamic dispatch table
  count = *(++data);
  type->method_count = count;
  type->methods = new void*[ count ];
  memset( type->methods, 0, count * sizeof(void*) );

  for (int i=0; i<count; ++i)
  {
    int index = *(++data);
    if (index >= 0) type->methods[i] = BardXC_methods[ index ];
  }

  // Aspect dispatch tables
  int aspect_count = *(++data);
  type->aspect_count = aspect_count;
  type->aspect_types = new BardXCType*[ aspect_count ];

  type->aspect_methods = new void**[ aspect_count ];
  memset( type->aspect_methods, 0, aspect_count * sizeof(void**) );

  for (int a=0; a<aspect_count; ++a)
  {
    type->aspect_types[a] = &BardXC_types[ *(++data) ];

    count = *(++data);
    type->aspect_methods[a] = new void*[ count ];
    memset( type->aspect_methods[a], 0, count * sizeof(void*) );
    for (int i=0; i<count; ++i)
    {
      int index = *(++data);
      if (index >= 0) type->aspect_methods[a][i] = BardXC_methods[ index ];
    }
  }
}


bool BardXCType_instance_of( BardXCType* type, int base_type_index )
{
  BardXCType* base_type = BardXC_types[ base_type_index ];

  if (type == base_type) return true;

  int count;
  if ((count = type->base_type_count))
  {
    BardXCType** base_types = type->base_types - 1;
    while (--count >= 0)
    {
      if (*(++base_types) == type) return true;
    }
  }

  return false;
}


static BardXCObject* BardXCReferenceArray_create( int type_index, int count )
{
  BardXCReferenceArray* array = (BardXCReferenceArray*) BardXC_create_object( type_index, count*sizeof(void*) );
  array->count = count;
  return (BardXCObject*) array;
}

static BardXCObject* BardXCReferenceArray_set( BardXCObject* array, int index, BardXCObject* value )
{
  ((BardXCReferenceArray*)array)->data[ index ] = value;
  return array;
}

