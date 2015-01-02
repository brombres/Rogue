#ifndef BARD_VM_H
#define BARD_VM_H

#include <setjmp.h>

#include "BardXC.h"

#define BardVM_clear_event_queue( vm )                   BardEventQueue_clear( (vm)->event_queue )
#define BardVM_write_event_integer( vm, value )          BardEventQueue_write_integer( (vm)->event_queue, value )
#define BardVM_write_event_real( vm, value )             BardEventQueue_write_real( (vm)->event_queue, value )
#define BardVM_write_event_byte( vm, value )             BardEventQueue_write_byte( (vm)->event_queue, value )
#define BardVM_write_event_bytes( vm, data, count )      BardEventQueue_write_bytes( (vm)->event_queue, data, count )
#define BardVM_write_event_characters( vm, data, count ) BardEventQueue_write_characters( (vm)->event_queue, data, count )

typedef union BardVMStackValue
{
  BardObject*   object;
  BardXCReal      real;
  BardXCInt64     integer64;
  BardXCInteger   integer;
} BardVMStackValue;

typedef struct BardVMStackFrame
{
  BardMethod*     m;
  BardXCInteger*    ip;
  BardVMStackValue* fp;
} BardVMStackFrame;

typedef struct BardVMNativeMethodInfo
{
  struct BardVMNativeMethodInfo* next_method_info;
  char*  type_context_name;
  char*  signature;
  BardVMNativeMethod function_pointer;

} BardVMNativeMethodInfo;

void BardVMNativeMethodInfo_destroy( BardVMNativeMethodInfo* info );

struct BardFileReader;

//=============================================================================
//  BardVM
//=============================================================================
typedef struct BardVM
{
  BardMM           mm;

  char**           identifiers;
  int              identifier_count;

  BardType**       types;
  int              type_count;

  BardMethod**     methods;
  int              method_count;

  BardVMNativeMethodInfo* native_methods;

  jmp_buf*         current_error_handler;
  int              error_code;
  char*            error_message;

  BardType*        type_Real;
  BardType*        type_Integer;
  BardType*        type_Character;
  BardType*        type_Byte;
  BardType*        type_Logical;

  BardType*        type_Object;
  BardType*        type_String;
  BardType*        type_StringBuilder;

  BardType*        type_LogicalList;
  BardType*        type_ByteList;
  BardType*        type_CharacterList;
  BardType*        type_IntegerList;
  BardType*        type_RealList;
  BardType*        type_ObjectList;
  BardType*        type_StringList;

  BardType*        type_Array_of_Logical;
  BardType*        type_Array_of_Byte;
  BardType*        type_Array_of_Character;
  BardType*        type_Array_of_Integer;
  BardType*        type_Array_of_Real;
  BardType*        type_Array_of_Object;

  BardType*        type_Random;

  BardType*        main_class;

  BardXCInteger*     code;
  int              code_size;
  BardObject**     literal_strings;
  int              literal_string_count;
  BardXCReal*        literal_reals;
  int              literal_real_count;
  BardObject**     singletons;

  BardVMStackValue*  sp;
  BardVMStackFrame*  current_frame;

  BardVMStackValue   stack[BARD_VM_STACK_SIZE];
  BardVMStackFrame   call_stack[BARD_VM_STACK_SIZE];

  int check_stack_limit;

  char**           filenames;
  int              filename_count;

  unsigned char*   line_info;
  int              line_info_count;

  //int gc_requested;

  BardEventQueue* event_queue;
  BardType*       type_EventQueue;
  BardMethod*     method_EventQueue_get_real_buffer;
  BardMethod*     method_EventQueue_get_character_buffer;
  BardMethod*     method_EventQueue_dispatch_events;

  BardVMMessageWriter* message_writer;
  BardVMMessageReader* message_reader;

  BardType*      type_MessageManager;
  BardMethod*    method_MessageManager_update;
  BardMethod*    method_MessageManager_get_native_message_buffer;
  BardHashTable* message_listeners;
  int            next_message_id;

  int              exit_requested;
  int              exit_code;
  int              tasks_finished;

  int BardList_data_offset;
  int BardList_count_offset;

  int BardString_characters_offset;
  int BardString_hash_code_offset;

  /*
  BardObject* objects;
  BardObject* objects_requiring_cleanup;
  //struct BardVMGlobalRef* global_refs;

  int gc_requested;
  int bytes_allocated_since_gc;
  */
} BardVM;

BardVM*   BardVM_create();  // calls init() automatically
BardVM*   BardVM_destroy( BardVM* vm );

BardVM*   BardVM_init( BardVM* vm );
BardVM*   BardVM_release( BardVM* vm );

void      BardVM_launch( BardVM* vm, int argc, const char* argv[] );
void      BardVM_update_until_finished( BardVM* vm, int updates_per_second );

void      BardVM_add_message_listener( BardVM* vm, const char* message_name, BardMessageListenerCallback fn, int ordering, void* user_data );
int       BardVM_remove_message_listener( BardVM* vm, const char* message_name, BardMessageListenerCallback fn );
BardVM*   BardVM_register_native_method( BardVM* vm, const char* type_context_name, const char* signature, BardVMNativeMethod fn_ptr );

int       BardVM_load_bc_file( BardVM* vm, const char* filename );
int       BardVM_load_bc_data( BardVM* vm, const char* filename, char* data, int count, int free_data );

void      BardVM_add_command_line_arguments( BardVM* vm, int argc, const char* argv[] );
void      BardVM_add_command_line_argument( BardVM* vm, const char* value );

void      BardVM_queue_message( BardVM* vm, const char* message_name );
void      BardVM_queue_message_with_id_and_origin( BardVM* vm, const char* message_name, int message_id, const char* origin_name );
void      BardVM_queue_message_arg( BardVM* vm, const char* arg_name, const char* format, ... );

void      BardVM_throw_error( BardVM* vm, int code, const char* message );

void      BardVM_set_sizes_and_offsets( BardVM* vm );
void      BardVM_organize( BardVM* vm );

void      BardVM_create_singletons( BardVM* vm );

BardType* BardVM_find_type( BardVM* vm, const char* type_name );
BardType* BardVM_must_find_type( BardVM* vm, const char* type_name );

int         BardVM_invoke_by_method_signature( BardVM* vm, BardType* type, const char* method_signature );
int         BardVM_invoke( BardVM* vm, BardType* type, BardMethod* m );

int           BardVM_read_line_info_integer( BardVM* vm, int* index );
BardMethod* BardVM_find_method_at_ip( BardVM* vm, int ip );
int           BardVM_find_line_info_at_ip( BardVM* vm, BardMethod* m, int ip, char** filename_pointer, int* line_number_pointer );

void        BardVM_create_main_object( BardVM* vm );

int         BardVM_update( BardVM* vm );
void        BardVM_dispatch_events( BardVM* vm );
void        BardVM_dispatch_messages( BardVM* vm );
void        BardVM_dispatch_messages_to_native_listeners( BardVM* vm );

void        BardVM_check_gc( BardVM* vm );
void        BardVM_collect_garbage( BardVM* vm );

BardObject* BardVM_collect_referenced_objects( BardVM* vm );  
// DANGER: if not used by GC then object sizes in returned list must be complemented before continuing.

/*
void        BardVM_call_clean_up( BardObject* list );
void        BardVM_free_object_list( BardObject* cur );
BardObject* BardVM_create_object( BardType* type, int size );

*/


#endif

