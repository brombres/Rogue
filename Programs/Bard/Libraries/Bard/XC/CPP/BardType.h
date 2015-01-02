#ifndef BARD_TYPE_H
#define BARD_TYPE_H

// Forward declarations
struct BardVM;
struct BardMethod;

//=============================================================================
//  BardType
//=============================================================================
typedef struct BardType
{
  struct BardVM*        vm;
  char*                 name;

  char**                tags;
  int                   tag_count;

  struct BardType**     base_types;
  int                   base_type_count;

  struct BardType*      element_type;  // for arrays
  int                   is_reference_array;

  struct BardProperty*  settings;
  int                   settings_count;
  unsigned char*        settings_data;

  struct BardProperty*  properties;
  int                   property_count;

  struct BardMethod**   methods;
  int                   method_count;

  int                   aspect_count;
  struct BardType**     aspect_types;
  struct BardMethod***  aspect_call_tables;

  BardXCInteger           attributes;
  BardXCInteger           object_size;
  BardXCInteger           reference_size;
  BardXCInteger           stack_slots;
  BardXCInteger           stack_size;   // # == stack slots * 8 bytes

  int                   trace_type;
  int                   reference_setting_count;
  int*                  reference_setting_offsets;
  int                   reference_property_count;
  int*                  reference_property_offsets;

  BardObject*           singleton;

  BardXCInteger           data_offset;

  struct BardMethod*    m_init_defaults;
  struct BardMethod*    m_destroy;
  struct BardMethod*    m_native_trace;

} BardType;

BardType*   BardType_create( struct BardVM* vm );
BardType*   BardType_init( BardType* type, char* name );
void        BardType_destroy( BardType* type );

void        BardType_set_reference_sizes( BardType* type );
void        BardType_set_up_settings( BardType* type );
void        BardType_set_property_offsets( BardType* type );
void        BardType_organize( BardType* type );

struct BardProperty* BardType_find_property( BardType* type, const char* name );

struct BardMethod* BardType_find_method( BardType* type, const char* signature );
BardObject*        BardType_create_object( BardType* type );
BardObject*        BardType_create_object_of_size( BardType* type, int byte_size );

int         BardType_partial_instance_of( BardType* type, BardType* ancestor_type );

#endif // BARD_TYPE_H
