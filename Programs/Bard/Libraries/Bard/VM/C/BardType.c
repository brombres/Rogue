#include "Bard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

BardType* BardType_create( BardVM* vm )
{
  BardType* type = (BardType*) CROM_SYSTEM_MALLOC( sizeof(BardType) );
  memset( type, 0, sizeof(BardType) );

  type->vm   = vm;

  //CromType_init( (CromType*) type, vm->crom );

  return type;
}

BardType* BardType_init( BardType* type, char* name )
{
  type->name = name;

  return type;
}

void BardType_destroy( BardType* type )
{
  if (type->tags)
  {
    CROM_SYSTEM_FREE( type->tags );
    type->tags = NULL;
    type->tag_count = 0;
  }

  if (type->base_types)
  {
    CROM_SYSTEM_FREE( type->base_types );
    type->base_types = NULL;
    type->base_type_count = 0;
  }

  if (type->settings)
  {
    CROM_SYSTEM_FREE( type->settings );
    CROM_SYSTEM_FREE( type->settings_data );
    type->settings_data = NULL;
    type->settings = NULL;
    type->settings_count = 0;
  }

  if (type->properties)
  {
    CROM_SYSTEM_FREE( type->properties );
    type->properties = NULL;
    type->property_count = 0;
  }

  if (type->methods)
  {
    CROM_SYSTEM_FREE( type->methods );
    type->methods = NULL;
    type->method_count = 0;
  }

  if (type->aspect_count)
  {
    CROM_SYSTEM_FREE( type->aspect_types );
    CROM_SYSTEM_FREE( type->aspect_call_tables );
    type->aspect_types = NULL;
    type->aspect_call_tables = NULL;
    type->aspect_count = 0;
  }

  if (type->reference_setting_offsets)
  {
    CROM_SYSTEM_FREE( type->reference_setting_offsets );
    type->reference_setting_offsets = 0;
    type->reference_setting_count = 0;
  }

  if (type->reference_property_offsets)
  {
    CROM_SYSTEM_FREE( type->reference_property_offsets );
    type->reference_property_offsets = 0;
    type->reference_property_count = 0;
  }

  CROM_SYSTEM_FREE( type );
}

void BardType_set_reference_sizes( BardType* type )
{
  BardVM* vm = type->vm;

  if (type->reference_size) return;  // recursively set

  if (BardType_is_primitive(type))
  {
    if (type == vm->type_Real)           type->reference_size = sizeof(BardReal);
    else if (type == vm->type_Integer)   type->reference_size = sizeof(BardInteger);
    else if (type == vm->type_Character) type->reference_size = sizeof(BardCharacter);
    else if (type == vm->type_Byte)      type->reference_size = 1;
    else if (type == vm->type_Logical)   type->reference_size = 1;

    type->stack_slots = 1;
    type->stack_size = sizeof(BardVMStackValue);
  }
  else if (BardType_is_reference(type))
  {
    type->reference_size = sizeof(BardObject*);
    type->stack_slots = 1;
    type->stack_size = sizeof(BardVMStackValue);
  }
  else
  {
    // Compound

    // Recursively set the reference size for each element while collecting the total size.
    int i;
    type->reference_size = -1;  // prevent recursion
    for (i=0; i<type->property_count; ++i)
    {
      BardProperty* property = &type->properties[i];
      if ( !property->type->reference_size )
      {
        BardType_set_reference_sizes( property->type );
      }
      type->stack_slots += property->type->stack_slots;
    }
    type->stack_size = sizeof(BardVMStackValue) * type->stack_slots;
    type->reference_size = type->stack_size;
  }
}

void BardType_set_up_settings( BardType* type )
{
  int next_offset = 0;
  int p;

  for (p=0; p<type->settings_count; ++p)
  {
    BardProperty* setting = &type->settings[p];
    next_offset = BardProperty_determine_offset( setting, next_offset );
  }

  // Pad to a multiple of 8 bytes
  while (next_offset & 7) ++next_offset;

  if (next_offset)
  {
    type->settings_data = (unsigned char*) CROM_SYSTEM_MALLOC( next_offset );
    memset( type->settings_data, 0, next_offset );
  }
}

void BardType_set_property_offsets( BardType* type )
{
  int next_offset = sizeof(BardObject);
  int p;

  if (BardType_is_reference(type))
  {
    for (p=0; p<type->property_count; ++p)
    {
      BardProperty* property = &type->properties[p];
      next_offset = BardProperty_determine_offset( property, next_offset );
    }

    // Pad to a multiple of 8 bytes
    while (next_offset & 7) ++next_offset;

    type->object_size = next_offset;
  }
  else if (BardType_is_primitive(type))
  {
    while (next_offset & 7) ++next_offset;
    type->data_offset = next_offset;

    next_offset += sizeof(BardVMStackValue);

    while (next_offset & 7) ++next_offset;
    type->object_size = next_offset;
  }
  else
  {
    while (next_offset & 7) ++next_offset;
    type->data_offset = next_offset;

    for (p=0; p<type->property_count; ++p)
    {
      BardProperty* property = &type->properties[p];
      property->offset = next_offset;
      next_offset += sizeof(BardVMStackValue);
    }

    while (next_offset & 7) ++next_offset;
    type->object_size = next_offset;
  }

  if (type->property_count > 0) type->data_offset = type->properties[0].offset;
//printf( "%s object size:%d, reference size:%d\n", type->name, type->object_size, type->reference_size );
}

void BardType_organize( BardType* type )
{
  int i, count;
  int dest_i;
  int ref_property_count;

  if (type->element_type && BardType_is_reference( type->element_type )) type->is_reference_array = 1;

  type->m_init_defaults = BardType_find_method( type, "init_defaults()" );
  type->m_destroy = BardType_find_method( type, "destroy()" );

  if (BardType_is_native(type))
  {
    type->m_native_trace  = BardType_find_method( type, "native_trace(Object)" );
  }

  // Make list of reference offsets
  count = type->settings_count;
  if (count)
  {
    type->reference_setting_count = 0;
    for (i=0; i<count; ++i)
    {
      if (BardType_is_reference( type->settings[i].type )) ++type->reference_setting_count;
    }

    type->reference_setting_offsets = (int*) CROM_SYSTEM_MALLOC( type->reference_setting_count * sizeof(int) );
    dest_i = 0;
    for (i=0; i<count; ++i)
    {
      BardProperty* setting = &type->settings[i];
      if (BardType_is_reference(setting->type)) type->reference_setting_offsets[dest_i++] = setting->offset;
    }
  }

  count = type->property_count;
  ref_property_count = 0;
  for (i=0; i<count; ++i)
  {
    if (BardType_is_reference( type->properties[i].type )) ++ref_property_count;
  }
  type->reference_property_count = ref_property_count;

  //CromType_set_reference_offset_capacity( (CromType*) type, ref_property_count );

  if (count)
  {
    type->reference_property_offsets = (int*) CROM_SYSTEM_MALLOC( type->reference_property_count * sizeof(int) );
    dest_i = 0;
    for (i=0; i<count; ++i)
    {
      BardProperty* property = &type->properties[i];
      if (BardType_is_reference(property->type))
      {
        type->reference_property_offsets[dest_i++] = property->offset;
        //CromType_add_reference_offset( (CromType*) type, property->offset );
      }
    }
  }

  if (type->is_reference_array)  type->trace_type = BARD_TRACE_TYPE_REFERENCE_ARRAY;
  else if (type->m_native_trace) type->trace_type = BARD_TRACE_TYPE_NATIVE_METHOD;
  else if (type->reference_property_count) type->trace_type = BARD_TRACE_TYPE_STANDARD;
  else                                     type->trace_type = BARD_TRACE_TYPE_SKIP;
}

BardProperty* BardType_find_property( BardType* type, const char* name )
{
  BardProperty* property = type->properties - 1;
  int count = type->property_count;
  while (--count >= 0)
  {
    if (0 == strcmp( (++property)->name, name )) return property;
  }

  return NULL;
}

BardMethod* BardType_find_method( BardType* type, const char* signature )
{
  int  i;
  int  signature_hash_code;

  if ( !type ) return NULL;

  signature_hash_code = BardVMCString_hash_code( signature );

  // Find method matching signature.
  for (i=type->method_count; --i >= 0;)
  {
    BardMethod* m = type->methods[i];
    if (m)
    {
      BardMethod_create_signature_if_necessary( m );
      if (m->signature_hash_code == signature_hash_code)
      {
        if (0 == strcmp(m->signature,signature))
        {
          return m;
        }
      }
    }
  }

  return NULL;
}

BardObject* BardType_create_object( BardType* type )
{
  BardVM* vm = type->vm;
  if (BardType_is_native(type))
  {
    BardMethod* m = BardType_find_method( type, "create()" );
    if (m && BardMethod_is_native(m) && m->return_type != NULL)
    {
      // Set 'm' in case of a missing native method error
      BardMethod* saved_m = vm->current_frame->m;
      vm->current_frame-> m = m;

      BardVM_push_object( vm, NULL );
      m->native_method( vm );

      vm->current_frame->m = saved_m;

      return BardVM_pop_object( vm );
    }
    else
    {
      char buffer[256];
      sprintf( buffer, "No native method create()->%s is defined and registered for native type %s.\n", type->name, type->name );
      BardVM_throw_error( vm, 1, buffer );
    }
  }

  if (type->element_type)
  {
    // Array
    return (BardObject*) BardArray_create_of_type( vm, type, type->element_type, 0 );
  }

  {
    return BardMM_create_object( &type->vm->mm, type, type->object_size );
  }
}

BardObject* BardType_create_object_of_size( BardType* type, int byte_size )
{
  return BardMM_create_object( &type->vm->mm, type, byte_size );
}

int BardType_partial_instance_of( BardType* type, BardType* ancestor_type )
{
  int i;
  BardType** cur_base_type;

  cur_base_type = type->base_types - 1;
  i = type->base_type_count;
  while (--i >= 0)
  {
    if (*(++cur_base_type) == ancestor_type) return 1;
    if (BardType_partial_instance_of(*cur_base_type,ancestor_type)) return 1;
  }

  return 0;
}
