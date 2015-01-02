#include "Bard.h"
#include "BardOpcodes.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
// for usleep()
#  include <unistd.h>
#endif


extern int bard_trace;

//=============================================================================
//  GLOBALS
//=============================================================================
BardVM* Bard_active_vm = NULL;


//=============================================================================
//  CALLBACKS
//=============================================================================
//void Bard_on_idle( BardVMMessageReader* reader, void* vm )
//{
  //((BardVM*)vm)->tasks_finished = 1;
//}

void Bard_on_exit_request( BardVMMessageReader* reader, void* vm )
{
  ((BardVM*)vm)->exit_requested = 1;
}


//=============================================================================
//  BardVMNativeMethodInfo
//=============================================================================
void BardVMNativeMethodInfo_destroy( BardVMNativeMethodInfo* info )
{
  while (info)
  {
    BardVMNativeMethodInfo* next = info->next_method_info;

    CROM_SYSTEM_FREE( info->type_context_name );
    CROM_SYSTEM_FREE( info->signature );
    CROM_SYSTEM_FREE( info );

    info = next;
  }
}

//=============================================================================
//  BardVM
//=============================================================================
BardVM* BardVM_create()
{
  BardVM* vm = CROM_SYSTEM_MALLOC( sizeof(BardVM) );
  return BardVM_init( vm );
}

BardVM* BardVM_destroy( BardVM* vm )
{
  if (vm) CROM_SYSTEM_FREE( BardVM_release(vm) );
  return NULL;
}

BardVM* BardVM_init( BardVM* vm )
{
  memset( vm, 0, sizeof(BardVM) );

  //vm->crom = Crom_create();
  BardMM_init( &vm->mm, vm );

  vm->sp = vm->stack + BARD_VM_STACK_SIZE;

  // Set up "current frame" to valid if meaningless values.
  vm->current_frame = (vm->call_stack + BARD_VM_STACK_SIZE) - 1;
  vm->current_frame->fp = vm->sp;
  vm->current_frame->ip = vm->code - 1;

  vm->event_queue = BardEventQueue_create();

  vm->message_writer = BardVMMessageWriter_create();
  vm->message_reader = BardVMMessageReader_create();

  vm->message_listeners = BardHashTable_create( 32 );
  vm->next_message_id = 1;

  vm->check_stack_limit = 1;

  return vm;
}

BardVM* BardVM_release( BardVM* vm )
{
  int i;

  vm->event_queue = BardEventQueue_destroy( vm->event_queue );

  vm->message_writer = BardVMMessageWriter_destroy( vm->message_writer );
  vm->message_reader = BardVMMessageReader_destroy( vm->message_reader );

  for (i=0; i<=vm->message_listeners->bin_mask; ++i)
  {
    BardHashTableEntry* entry = vm->message_listeners->bins[i];
    while (entry)
    {
      BardMessageListener* listener = (BardMessageListener*) entry->value;
      while (listener)
      {
        BardMessageListener* next_listener = listener->next_listener;
        BardMessageListener_destroy( listener );
        listener = next_listener;
      }
      entry->value = NULL;
      entry = entry->next_entry;
    }
  }

  BardMM_free_data( &vm->mm );

  if (vm->line_info)
  {
    CROM_SYSTEM_FREE( vm->line_info );
    vm->line_info = NULL;
    vm->line_info_count = 0;
  }

  BardVMNativeMethodInfo_destroy( vm->native_methods );

  if (vm->error_message)
  {
    CROM_SYSTEM_FREE( vm->error_message );
    vm->error_message = NULL;
  }

  if (vm->singletons)
  {
    CROM_SYSTEM_FREE( vm->singletons );
    vm->singletons = NULL;
  }

  if (vm->literal_reals)
  {
    CROM_SYSTEM_FREE( vm->literal_reals );
    vm->literal_reals = NULL;
  }

  if (vm->literal_strings)
  {
    CROM_SYSTEM_FREE( vm->literal_strings );
    vm->literal_strings = NULL;
  }

  if (vm->code)
  {
    CROM_SYSTEM_FREE( vm->code );
    vm->code = NULL;
    vm->code_size = 0;
  }

  for (i=0; i<vm->method_count; ++i)
  {
    BardMethod_destroy( (BardMethod*) vm->methods[i] );
  }
  CROM_SYSTEM_FREE( vm->methods );
  vm->methods = NULL;

  for (i=0; i<vm->type_count; ++i)
  {
    BardType_destroy( (BardType*) vm->types[i] );
  }
  CROM_SYSTEM_FREE( vm->types );
  vm->types = NULL;

  for (i=0; i<vm->identifier_count; ++i)
  {
    CROM_SYSTEM_FREE( vm->identifiers[i] );
  }
  CROM_SYSTEM_FREE( vm->identifiers );
  vm->identifiers = NULL;

  //Crom_destroy( vm->crom );

  return vm;
}

void BardVM_launch( BardVM* vm, int argc, const char* argv[] )
{
  BardStandardLibrary_configure( vm );

  BardVM_add_command_line_arguments( vm, argc, argv );

  BardVM_create_main_object( vm );
}

void BardVM_update_until_finished( BardVM* vm, int updates_per_second )
{
  while (BardVM_update(vm))
  {
    usleep( 1000000/updates_per_second );
  }
}

void BardVM_add_message_listener( BardVM* vm, const char* message_name, BardMessageListenerCallback fn, int ordering, void* user_data )
{
  BardMessageListener* new_listener = BardMessageListener_create(  message_name, fn, ordering, user_data );
  BardMessageListener* existing_listener = (BardMessageListener*) BardHashTable_get( vm->message_listeners, message_name );

  if ( !existing_listener || ordering == BARD_REPLACE_MESSAGE_LISTENER )
  {
    BardHashTable_set( vm->message_listeners, message_name, new_listener );
    return;
  }

  if (ordering == BARD_INSERT_MESSAGE_LISTENER)
  {
    new_listener->next_listener = existing_listener;
    BardHashTable_set( vm->message_listeners, message_name, new_listener );
  }
  else if (ordering == BARD_APPEND_MESSAGE_LISTENER)
  {
    while (existing_listener->next_listener) existing_listener = existing_listener->next_listener;
    existing_listener->next_listener = new_listener;
  }
}

int BardVM_remove_message_listener( BardVM* vm, const char* message_name, BardMessageListenerCallback fn )
{
  BardMessageListener* cur = (BardMessageListener*) BardHashTable_get( vm->message_listeners, message_name );

  if (cur)
  {
    // Listener is first in list
    BardHashTable_set( vm->message_listeners, message_name, cur->next_listener );
    BardMessageListener_destroy( cur );
    return 1;
  }

  // Listener is later in list if present 
  while (cur->next_listener)
  {
    BardMessageListener* next_listener = cur->next_listener;
    if (next_listener->fn == fn && 0 == strcmp(message_name,next_listener->name))
    {
      cur->next_listener = next_listener->next_listener;
      BardMessageListener_destroy( next_listener );
      return 1;
    }
    cur = next_listener;
  }

  return 0;
}

BardVM* BardVM_register_native_method( BardVM* vm, const char* type_context_name, const char* signature, BardVMNativeMethod fn_ptr )
{
  // First check for existing native method handler to override.
  BardVMNativeMethodInfo* info = vm->native_methods;
  while (info)
  {
    if (0 == strcmp(info->type_context_name,type_context_name) && 0 == strcmp(info->signature,signature))
    {
      // Found it.  Free the particulars to be replaced.
      CROM_SYSTEM_FREE( info->type_context_name );
      CROM_SYSTEM_FREE( info->signature );
      break;
    }

    info = info->next_method_info;
  }

  if ( !info )
  {
    info = (BardVMNativeMethodInfo*) CROM_SYSTEM_MALLOC( sizeof(BardVMNativeMethodInfo) );
    memset( info, 0, sizeof(BardVMNativeMethodInfo) );
    info->next_method_info = vm->native_methods;
    vm->native_methods = info;
  }

  info->type_context_name = BardVMCString_duplicate( type_context_name );
  info->signature   = BardVMCString_duplicate( signature );
  info->function_pointer = fn_ptr;

  return vm;
}

int BardVM_load_bc_file( BardVM* vm, const char* filename )
{
  BardFileReader* reader = BardFileReader_create_with_filename( filename );
  if ( !reader ) return 0;

  {
    int result = BardBCLoader_load_bc_from_reader( vm, reader );
    BardFileReader_destroy( reader );
    return result;
  }
}

int BardVM_load_bc_data( BardVM* vm, const char* filename, char* data, int count, int free_data )
{
  BardFileReader* reader = BardFileReader_create_with_data( filename, data, count, free_data );
  if ( !reader ) return 0;

  {
    int result = BardBCLoader_load_bc_from_reader( vm, reader );
    BardFileReader_destroy( reader );
    return result;
  }
}

void BardVM_add_command_line_arguments( BardVM* vm, int argc, const char* argv[] )
{
  int i;
  for (i=1; i<argc; ++i)
  {
    BardVM_add_command_line_argument( vm, argv[i] );
  }
}

void BardVM_add_command_line_argument( BardVM* vm, const char* value )
{
  BardType* type_Runtime = BardVM_find_type( vm, "Runtime" );
  if (type_Runtime)
  {
    BardMethod* m_add_command_line_argument = BardType_find_method( type_Runtime, "add_command_line_argument(String)" );
    if (m_add_command_line_argument)
    {
      BardVM_push_object( vm, type_Runtime->singleton );
      BardVM_push_c_string( vm, value );
      BardVM_invoke( vm, type_Runtime, m_add_command_line_argument );
    }
  }
}

// Message Message Passing
void BardVM_queue_message( BardVM* vm, const char* message_name )
{
  BardVM_queue_message_with_id_and_origin( vm, message_name, vm->next_message_id++, "vm" );
}

void BardVM_queue_message_with_id_and_origin( BardVM* vm, const char* message_name, int message_id, const char* origin_name )
{
  if ( !vm->message_writer ) return;

  BardVMMessageWriter_begin_message( vm->message_writer, message_name, message_id, origin_name );
}

void BardVM_queue_message_arg( BardVM* vm, const char* arg_name, const char* format, ... )
{
  va_list args;

  if ( !vm->message_writer ) return;

  va_start( args, format );
  BardVMMessageWriter_write_arg( vm->message_writer, arg_name, format, args );
  va_end(args);
}

void BardVM_throw_error( BardVM* vm, int code, const char* message )
{
  if (vm->error_message)
  {
    CROM_SYSTEM_FREE( vm->error_message );
    vm->error_message = NULL;
  }

  vm->error_code = code;
  if (message)
  {
    vm->error_message = CROM_SYSTEM_MALLOC( strlen(message)+1 );
    strcpy( vm->error_message, message );
  }

  if (vm->current_error_handler)
  {
    longjmp( *(vm->current_error_handler), code );
  }
  else
  {
    printf( "ERROR: No error handler set.\n" );
    printf( "ERROR: %s.\n", message );
  }
}

void BardVM_set_sizes_and_offsets( BardVM* vm )
{
  // Locate important types
  vm->type_Real      = BardVM_must_find_type( vm, "Real" );
  vm->type_Integer   = BardVM_must_find_type( vm, "Integer" );
  vm->type_Character = BardVM_must_find_type( vm, "Character" );
  vm->type_Byte      = BardVM_must_find_type( vm, "Byte" );
  vm->type_Logical   = BardVM_must_find_type( vm, "Logical" );

  vm->type_Object        = BardVM_must_find_type( vm, "Object" );
  vm->type_String        = BardVM_must_find_type( vm, "String" );
  vm->type_StringBuilder = BardVM_must_find_type( vm, "StringBuilder" );

  vm->type_Array_of_Logical   = BardVM_find_type( vm, "Array<<Logical>>" );
  vm->type_Array_of_Byte      = BardVM_find_type( vm, "Array<<Byte>>" );
  vm->type_Array_of_Character = BardVM_find_type( vm, "Array<<Character>>" );
  vm->type_Array_of_Integer   = BardVM_find_type( vm, "Array<<Integer>>" );
  vm->type_Array_of_Real      = BardVM_find_type( vm, "Array<<Real>>" );
  vm->type_Array_of_Object    = BardVM_find_type( vm, "Array<<Object>>" );

  vm->type_ByteList      = BardVM_find_type( vm, "Byte[]" );
  vm->type_LogicalList   = BardVM_find_type( vm, "Logical[]" );
  vm->type_CharacterList = BardVM_find_type( vm, "Character[]" );
  vm->type_IntegerList   = BardVM_find_type( vm, "Integer[]" );
  vm->type_RealList      = BardVM_find_type( vm, "Real[]" );
  vm->type_ObjectList    = BardVM_find_type( vm, "Object[]" );
  vm->type_StringList    = BardVM_find_type( vm, "String[]" );

  vm->type_Random        = BardVM_find_type( vm, "Random" );

  // Set each type's reference size
  {
    int i;
    for (i=0; i<vm->type_count; ++i)
    {
      BardType* type = vm->types[i];
      BardType_set_reference_sizes( type );
    }
  }

  // Set each type's property offsets
  {
    int i;
    for (i=0; i<vm->type_count; ++i)
    {
      BardType_set_up_settings( vm->types[i] );
      BardType_set_property_offsets( vm->types[i] );
    }
  }

  // Collect various types, methods and property offsets
  vm->BardList_data_offset = BardType_find_property( vm->type_ObjectList, "data" )->offset;
  vm->BardList_count_offset = BardType_find_property( vm->type_ObjectList, "count" )->offset;
}

void BardVM_organize( BardVM* vm )
{
  // Organize each type
  {
    int i;
    for (i=0; i<vm->type_count; ++i)
    {
      BardType* type = vm->types[i];
      BardType_organize( type );
    }
  }

  // Organize each method
  {
    int i;
    for (i=0; i<vm->method_count; ++i)
    {
      BardMethod* m = vm->methods[i];
      BardMethod_organize( m );
    }
  }

  // Assign native methods
  {
    // Reset each [native] method to throw an error by default
    {
      int i;
      for (i=0; i<vm->method_count; ++i)
      {
        BardMethod* m = vm->methods[i];
        if (BardMethod_is_native(m)) 
        {
          m->native_method = BardProcessor_on_unhandled_native_method;
          m->ip[1] = BARD_VM_CREATE_OP_WITH_ARG_N( BARD_VM_OP_NATIVE_CALL_INDEX_N, m->global_index );
          m->ip[2] = BARD_VM_CREATE_OP_WITH_ARG_N( BARD_VM_OP_NATIVE_RETURN, m->global_index );
        }
      }
    }

    // Assign registered native methods
    BardVMNativeMethodInfo* cur = vm->native_methods;
    while (cur)
    {
      BardType* type = BardVM_find_type( vm, cur->type_context_name );
      if (type)
      {
        BardMethod* m = BardType_find_method( type, cur->signature );
        if (m)
        {
          m->local_slot_count = 0;
          m->native_method = cur->function_pointer;
          m->ip[1] = BARD_VM_CREATE_OP_WITH_ARG_N( BARD_VM_OP_NATIVE_CALL_INDEX_N, m->global_index );
          m->ip[2] = BARD_VM_CREATE_OP_WITH_ARG_N( BARD_VM_OP_NATIVE_RETURN, m->global_index );
        }
      }
      cur = cur->next_method_info;
    }
  }

  // Make sure that every type with a native_trace() has a registered native function.
  {
    int i;
    for (i=0; i<vm->type_count; ++i)
    {
      BardType* type = vm->types[i];
      if (type->m_native_trace && !type->m_native_trace->native_method)
      {
        char st[256];
        sprintf( st, "No native implementation of %s::native_trace(Object) is registered.", type->name );
        BardVM_throw_error( vm, 1, st );
      }
    }
  }
}

void BardVM_create_singletons( BardVM* vm )
{
  // Create singletons
  vm->singletons = (BardObject**) CROM_SYSTEM_MALLOC( sizeof(BardObject*) * vm->type_count );

  {
    // Create the actual objects
    int i;
    for (i=0; i<vm->type_count; ++i)
    {
      BardType* type = vm->types[i];
      BardObject* singleton = BardType_create_object( type );
      type->singleton = singleton;
      vm->singletons[i] = singleton;
    }

    // Call non-inherited init_settings() on each singleton
    for (i=0; i<vm->type_count; ++i)
    {
      BardType* type = vm->types[i];
      BardMethod* m_init_settings = BardType_find_method( type, "init_settings()" );
      if (m_init_settings && m_init_settings->type_context == type)
      {
        BardVM_push_object( vm, type->singleton );
        BardProcessor_invoke( vm, type, m_init_settings );
      }
    }

    // Call init_defaults() on each singleton officially marked [singleton]
    for (i=0; i<vm->type_count; ++i)
    {
      BardType* type = vm->types[i];
      if (BardType_is_singleton(type) && type->m_init_defaults)
      {
        BardVM_push_object( vm, type->singleton );
        BardProcessor_invoke( vm, type, type->m_init_defaults );
        BardVM_pop_discard( vm );
      }
    }
//printf("pt4\n");

    // Call init() if it exists on each singleton officially marked [singleton]
    // that's not the main class.
    for (i=0; i<vm->type_count; ++i)
    {
      BardType* type = vm->types[i];
      if (BardType_is_singleton(type) && type != vm->main_class)
      {
//printf("pt4.1 %s\n", type->name);
        BardMethod* m_init = BardType_find_method( type, "init()" );
        if (m_init)
        {
          BardVM_push_object( vm, type->singleton );
//printf("pt4.2.2 %s\n", BardMethod_signature(m_init) );
//if (strcmp(type->name,"Templates")==0) bard_trace = 1;
          BardProcessor_invoke( vm, type, m_init );
//bard_trace = 0;
//printf("pt4.2.3\n");
          ++vm->sp;
        }
      }
    }
  }

  if (vm->message_writer)
  {
    vm->type_MessageManager = BardVM_find_type( vm, "MessageManager" );
    if (vm->type_MessageManager)
    {
      vm->method_MessageManager_get_native_message_buffer = BardType_find_method
      (
        vm->type_MessageManager, 
        "get_native_message_buffer(Integer)"
      );

      vm->method_MessageManager_update = BardType_find_method
      (
          vm->type_MessageManager, 
          "update()" 
      );
    }
    if ( !(vm->type_MessageManager && vm->method_MessageManager_update && vm->method_MessageManager_get_native_message_buffer) )
    {
      vm->type_MessageManager = NULL;
      vm->method_MessageManager_update = NULL;
      vm->method_MessageManager_get_native_message_buffer = NULL;
      vm->message_writer = BardVMMessageWriter_destroy( vm->message_writer );
    }
  }

  vm->type_EventQueue = BardVM_find_type( vm, "EventQueue" );
  if (vm->type_EventQueue)
  {
    vm->method_EventQueue_get_character_buffer = BardType_find_method( vm->type_EventQueue, "get_character_buffer(Integer)" );
    vm->method_EventQueue_get_real_buffer = BardType_find_method( vm->type_EventQueue, "get_real_buffer(Integer)" );
    vm->method_EventQueue_dispatch_events = BardType_find_method( vm->type_EventQueue, "dispatch_events()" );
    if (!vm->method_EventQueue_get_character_buffer || !vm->method_EventQueue_get_real_buffer || !vm->method_EventQueue_dispatch_events)
    {
      vm->method_EventQueue_get_character_buffer = NULL;
      vm->method_EventQueue_get_real_buffer = NULL;
      vm->method_EventQueue_dispatch_events = NULL;
      vm->type_EventQueue = NULL;
    }
  }
}

BardType* BardVM_find_type( BardVM* vm, const char* type_name )
{
  int i = vm->type_count;
  while (--i >= 0)
  {
    BardType* cur_type = vm->types[i];
    if (0 == strcmp(cur_type->name,type_name)) return cur_type;
  }
  return NULL;
}

BardType* BardVM_must_find_type( BardVM* vm, const char* type_name )
{
  BardType* type;
  type = BardVM_find_type( vm, type_name );

  if ( !type )
  {
    char st[80];
    if (strlen(type_name) > 50) strcpy( st, "Missing required type." );
    else                        sprintf( st, "Missing required type %s.", type_name );
    BardVM_throw_error( vm, 1, st );
  }

  return type;
}

int BardVM_invoke_by_method_signature( BardVM* vm, BardType* type, const char* method_signature )
{
  BardMethod* m;

  if ( !type ) return 0;

  m = BardType_find_method( type, method_signature );
  if ( !m ) return 0;

  return BardVM_invoke( vm, type, m );
}

int BardVM_invoke( BardVM* vm, BardType* type, BardMethod* m )
{
  if ( !type || !m ) return 0;

  BardProcessor_invoke( vm, type, m );
  return (0 == vm->error_code);
}

int BardVM_read_line_info_integer( BardVM* vm, int* index )
{
  // 0: 0xxxxxxx - 0..127
  // 1: 1000xxxx - 1 byte follows (12 bits total, unsigned)
  // 2: 1001xxxx - 2 bytes follow (20 bits total, unsigned)
  // 3: 1010xxxx - 3 bytes follow (28 bits total, unsigned)
  // 4: 10110000 - 4 bytes follow (32 bits total, signed)
  // 5: 11xxxxxx - -64..-1
  int b1;
  
  b1 = vm->line_info[ (*index)++ ];

  if (b1 < 128)  return b1;          // encoding 0
  if (b1 >= 192) return (b1 - 256);  // encoding 5

  switch (b1 & 0x30)
  {
    case 0:  // encoding 1
      return ((b1 & 15) << 8) | vm->line_info[ (*index)++ ];

    case 16:  // encoding 2
      {
        int b2 = vm->line_info[ (*index)++ ];
        int b3 = vm->line_info[ (*index)++ ];
        return ((b1 & 15) << 16) | (b2 << 8) | b3;
      }

    case 32:  // encoding 3
      {
        int b2 = vm->line_info[ (*index)++ ];
        int b3 = vm->line_info[ (*index)++ ];
        int b4 = vm->line_info[ (*index)++ ];
        return ((b1 & 15) << 24) | (b2 << 16) | (b3 << 8) | b4;
      }

    case 48:  // encoding 4
      {
        int result = vm->line_info[ (*index)++ ];
        result = (result << 8) | vm->line_info[ (*index)++ ];
        result = (result << 8) | vm->line_info[ (*index)++ ];
        result = (result << 8) | vm->line_info[ (*index)++ ];
        return (BardInteger) result;
      }

    default:
      return -1;
  }
}

BardMethod* BardVM_find_method_at_ip( BardVM* vm, int ip )
{
  int i;
  for (i=0; i<vm->method_count; ++i)
  {
    BardMethod* m = vm->methods[i];
    if (ip >= m->ip_start && ip < m->ip_limit) return m;
  }
  return NULL;
}

int BardVM_find_line_info_at_ip( BardVM* vm, BardMethod* m, int ip, char** filename_pointer, int* line_number_pointer )
{
  if (m)
  {
    int line_info_index = m->line_info_index;

    int file_switch_count = BardVM_read_line_info_integer( vm, &line_info_index );
    int f;

    int file_index=0, cur_line=0;
    for (f=0; f<file_switch_count; ++f)
    {
      file_index = BardVM_read_line_info_integer( vm, &line_info_index );
      cur_line   = BardVM_read_line_info_integer( vm, &line_info_index );
      int cur_ip     = BardVM_read_line_info_integer( vm, &line_info_index );
      int delta_count = BardVM_read_line_info_integer( vm, &line_info_index );
      int d;
      for (d=0; d<delta_count; ++d)
      {
        int next_line = cur_line + BardVM_read_line_info_integer( vm, &line_info_index );
        int next_ip   = cur_ip   + BardVM_read_line_info_integer( vm, &line_info_index );
        if (ip < next_ip) break;

        cur_line = next_line;
        cur_ip = next_ip;
      }
      *filename_pointer = vm->filenames[ file_index ];
      *line_number_pointer = cur_line;
      return 1;
    }

    *filename_pointer = vm->filenames[ file_index ];
    *line_number_pointer = cur_line;

    return 1;
  }
  else
  {
    return 0;
  }
}

void BardVM_create_main_object( BardVM* vm )
{
  BardType*    main_class = vm->main_class;
  BardMethod*  m_init = BardType_find_method( main_class, "init()" );

  if (m_init)
  {
    BardVM_push_object( vm, main_class->singleton );
    BardVM_invoke( vm, main_class, m_init );
    BardVM_pop_discard( vm );
  }

  BardVM_queue_message( vm, "App.launch" );
}

int BardVM_update( BardVM* vm )
{
  if (vm->message_writer)
  {
    BardVM_dispatch_messages( vm );
  }

  BardVM_write_event_integer( vm, BARD_EVENT_UPDATE );
  BardVM_dispatch_events( vm );

  BardVM_check_gc( vm );

  return !(vm->exit_requested || vm->tasks_finished);
}

void BardVM_dispatch_messages( BardVM* vm )
{
  if (vm->message_writer)
  {
    BardByte* message_characters = BardVMMessageWriter_to_c_string( vm->message_writer );
    int   length = vm->message_writer->count;
    BardObject* message_buffer;

    // Fetch Bard-side message buffer with enough capacity to store all native messages.
    BardVM_push_object( vm, vm->type_MessageManager->singleton );
    BardVM_push_integer( vm, length );
    BardVM_invoke( vm, vm->type_MessageManager, vm->method_MessageManager_get_native_message_buffer );
    message_buffer = BardVM_pop_object( vm );

    if (message_buffer)
    {
      BardCharacter* dest;

      // Prepare the native message queue to be dispatched to native listeners...
      BardVMMessageReader_set_c_string_data( vm->message_reader, (char*) message_characters, length );

      // .. . as well as Bard listeners
      dest = (BardList_data(message_buffer))->character_data - 1;
      BardList_count(message_buffer) = length;

      --message_characters;
      while (--length >= 0) *(++dest) = *(++message_characters);

      BardVMMessageWriter_clear( vm->message_writer );

      // Dispatch the native queue TO NATIVE LISTENERS before sending it to the Bard side
      BardVM_dispatch_messages_to_native_listeners( vm );

      // Have Bard dispatch our native messages and get Bard-side messages to be dispatched here
      BardVM_push_object( vm, vm->type_MessageManager->singleton );
      BardVM_invoke( vm, vm->type_MessageManager, vm->method_MessageManager_update );
      message_buffer = BardVM_pop_object( vm );

      if (message_buffer)
      {
        int                count = BardList_count( message_buffer );
        BardCharacter*     data = BardList_data( message_buffer )->character_data;

        BardVMMessageReader_set_character_data( vm->message_reader, data, count );
        BardVM_dispatch_messages_to_native_listeners( vm );
      }
    }
  }
}

void BardVM_dispatch_messages_to_native_listeners( BardVM* vm )
{
  BardVMMessageReader* reader = vm->message_reader;
  while (BardVMMessageReader_has_another(reader))
  {
    char type_name_buffer[512];
    BardVMMessageReader_read_c_string( reader, "m_type", type_name_buffer, 512 );
    if (type_name_buffer[0])
    {
      BardMessageListener* cur = (BardMessageListener*) BardHashTable_get( vm->message_listeners, type_name_buffer );
      while (cur)
      {
        cur->fn( reader, cur->user_data );
        cur = cur->next_listener;
      }
    }
    BardVMMessageReader_advance( reader );
  }
}

void BardVM_dispatch_events( BardVM* vm )
{
  if (vm->type_EventQueue)
  {
    BardArray* character_buffer;
    BardArray* real_buffer;

    BardVM_push_object( vm, vm->type_EventQueue->singleton );
    BardVM_push_integer( vm, vm->event_queue->character_count );
    BardVM_invoke( vm, vm->type_EventQueue, vm->method_EventQueue_get_character_buffer );
    character_buffer = (BardArray*) BardVM_pop_object( vm );

    BardVM_push_object( vm, vm->type_EventQueue->singleton );
    BardVM_push_integer( vm, vm->event_queue->real_count );
    BardVM_invoke( vm, vm->type_EventQueue, vm->method_EventQueue_get_real_buffer );
    real_buffer = (BardArray*) BardVM_pop_object( vm );

    memcpy( character_buffer->character_data, vm->event_queue->character_data, vm->event_queue->character_count * sizeof(BardCharacter) );
    memcpy( real_buffer->real_data, vm->event_queue->real_data, vm->event_queue->real_count * sizeof(BardReal) );

    // Be sure and clear event queue before allowing Bard to dispatch the events (which may indirectly create new events)
    BardEventQueue_clear( vm->event_queue );

    BardVM_push_object( vm, vm->type_EventQueue->singleton );
    BardVM_invoke( vm, vm->type_EventQueue, vm->method_EventQueue_dispatch_events );
    vm->tasks_finished = !BardVM_pop_logical( vm );
  }
  else
  {
    BardEventQueue_clear( vm->event_queue );
    vm->tasks_finished = 1;
  }
}

void BardVM_check_gc( BardVM* vm )
{
  if (vm->mm.bytes_allocated_since_gc >= BARD_VM_BYTES_BEFORE_AUTO_GC)
  {
    BardVM_collect_garbage( vm );
  }
  //else printf( "%d\n", vm->mm.bytes_allocated_since_gc );
}

void BardVM_collect_garbage( BardVM* vm )
{
  //Crom* crom = vm->crom;
  BardMM* mm = &vm->mm;
  vm->mm.bytes_allocated_since_gc = 0;

  // Trace through all references.
  BardVM_collect_referenced_objects( vm );

  // Free any unreferenced objects.
  {
    BardObject* remaining = NULL;
    BardObject* cur = mm->objects;
    while (cur)
    {
      BardObject* next = cur->next_allocation;
      if (cur->size < 0)
      {
        // Referenced; link into remaining list.
        cur->size = ~cur->size;
        cur->next_allocation = remaining;
        remaining = cur;
      }
      else
      {
        // Unreferenced; free allocation.
        Crom_free( crom, cur, cur->size );
      }
      cur = next;
    }
    mm->objects = remaining;
  }

  // Call destroy() on any unreferenced objects requiring cleanup,
  // mark them as referenced, and move them to the regular object list.
  // This must be done after unreferenced objects are freed because
  // destroy() could theoretically introduce all objects in an
  // an unreferenced island back into the main set.
  {
    BardObject* cur = mm->objects_requiring_cleanup;
    mm->objects_requiring_cleanup = NULL;
    while (cur)
    {
      BardObject* next = cur->next_allocation;
      if (cur->size < 0)
      {
        // Referenced; leave on objects_requiring_cleanup list.
        cur->size = ~cur->size;
        cur->next_allocation = mm->objects_requiring_cleanup;
        mm->objects_requiring_cleanup = cur;
      }
      else
      {
        // Unreferenced; move to regular object list and call destroy().
        // Note that destroy() could create additional new objects
        // which is why we can't catch mm->objects_requiring_cleanup[] and
        // mm->objects[] as local lists.
        cur->next_allocation = mm->objects;
        mm->objects = cur;

        BardVM_push_object( vm, cur );
        BardVM_invoke( vm, cur->type, cur->type->m_destroy );
      }
      cur = next;
    }
  }
}

BardObject* BardVM_collect_referenced_objects( BardVM* vm )
{
  BardObject* pending_objects = NULL;
  BardObject* referenced_objects = NULL;

  // Collect singletons, reference settings, and literal strings
  {
    int type_count = vm->type_count;
    BardObject** singletons = vm->singletons - 1;
    while (--type_count >= 0)
    {
      BardObject* singleton;
      if ((singleton = *(++singletons)))
      {
        int count;
        BardType* type = singleton->type;

        Bard_collect_reference( singleton, pending_objects );

        if ((count = type->reference_setting_count))
        {
          unsigned char* settings_data = type->settings_data;
          int* reference_setting_offsets = type->reference_setting_offsets - 1;
          while (--count >= 0)
          {
            Bard_collect_reference( *((BardObject**)(settings_data + *(++reference_setting_offsets))), pending_objects );
          }
        }
      }
    }

    int count = vm->literal_string_count;
    while (--count >= 0)
    {
      Bard_collect_reference( vm->literal_strings[count], pending_objects );
    }
  }

  // Trace through each object on the 'pending_objects' queue until queue is empty.
  while (pending_objects)
  {
    // Remove the next object off the pending list and add it to the list of all referenced objects.
    BardObject* obj = pending_objects;
    BardType* type = obj->type;
    pending_objects = pending_objects->next_reference;
    obj->next_reference = referenced_objects;
    referenced_objects = obj;

    // Collect references in the current object.
    switch (type->trace_type)
    {
      case BARD_TRACE_TYPE_STANDARD:
        {
          int  count = type->reference_property_count;
          int* cur_offset = type->reference_property_offsets - 1;
          while (--count >= 0)
          {
            Bard_collect_reference( (*((BardObject**)(((BardByte*)obj) + *(++cur_offset)))), pending_objects );
          }
        }
        continue;

      case BARD_TRACE_TYPE_REFERENCE_ARRAY:
        {
          int count = ((BardArray*) obj)->count;
          BardObject** data = ((BardArray*) obj)->object_data - 1;
          while (--count >= 0)
          {
            Bard_collect_reference( *(++data), pending_objects );
          }
        }
        continue;

      case BARD_TRACE_TYPE_NATIVE_METHOD:
        BardVM_push_object( vm, obj );
        BardVM_push_object( vm, pending_objects );
        type->m_native_trace->native_method( vm );
        pending_objects = BardVM_pop_object( vm );
        continue;

      default:
        continue;
    }
  }

  return referenced_objects;
}

