#ifndef BARD_DEFINES_H
#define BARD_DEFINES_H

#ifndef BARD_VM_STACK_SIZE
#  define BARD_VM_STACK_SIZE 4096
//#  define BARD_VM_STACK_SIZE 100
#endif

#ifndef BARD_VM_LOG
#  define BARD_VM_LOG(mesg) printf(mesg); printf("\n")
#endif

#ifndef CROM_SYSTEM_MALLOC
#  define CROM_SYSTEM_MALLOC(bytes) malloc(bytes)
#endif

#ifndef CROM_SYSTEM_FREE
#  define CROM_SYSTEM_FREE(ptr) free(ptr)
#endif

#ifndef BARD_VM_BYTES_BEFORE_AUTO_GC
#  define BARD_VM_BYTES_BEFORE_AUTO_GC (512*1024)
#endif

#ifndef NULL
#  define NULL 0
#endif

#if defined(_WIN32)
  typedef __int32          BardInteger;
  typedef unsigned __int16 BardCharacter;
  typedef unsigned char    BardByte;
  typedef double           BardReal;
  typedef BardInteger      BardLogical;
  typedef float            BardReal32;
  typedef __int64          BardInt64;
#else
  typedef int              BardInteger;
  typedef unsigned short   BardCharacter;
  typedef unsigned char    BardByte;
  typedef double           BardReal;
  typedef BardInteger      BardLogical;
  typedef float            BardReal32;
  typedef long long        BardInt64;
#endif

struct BardVM;

typedef void (*BardVMNativeMethod)(struct BardVM* vm);

#define Bard_allocate(type)             ((type*)Bard_allocate_bytes_and_clear(sizeof(type)))
#define Bard_allocate_array(type,count) ((type*)Bard_allocate_bytes_and_clear(count*sizeof(type)))

#define BARD_VM_ATTRIBUTE_TYPE_MASK  3
#define BARD_VM_ATTRIBUTE_CLASS      0
#define BARD_VM_ATTRIBUTE_ASPECT     1
#define BARD_VM_ATTRIBUTE_PRIMITIVE  2
#define BARD_VM_ATTRIBUTE_COMPOUND   3

#define BARD_VM_ATTRIBUTE_NATIVE     4
#define BARD_VM_ATTRIBUTE_SINGLETON  16

#define BARD_VM_ATTRIBUTE_REQUIRES_CLEANUP 8192

#define BardType_is_class( type )     ((type->attributes & BARD_VM_ATTRIBUTE_TYPE_MASK) == BARD_VM_ATTRIBUTE_CLASS)
#define BardType_is_aspect( type )    ((type->attributes & BARD_VM_ATTRIBUTE_TYPE_MASK) == BARD_VM_ATTRIBUTE_ASPECT)
#define BardType_is_reference( type ) ((type->attributes & BARD_VM_ATTRIBUTE_TYPE_MASK) <= BARD_VM_ATTRIBUTE_ASPECT)
#define BardType_is_primitive( type ) ((type->attributes & BARD_VM_ATTRIBUTE_TYPE_MASK) == BARD_VM_ATTRIBUTE_PRIMITIVE)
#define BardType_is_compound( type )  ((type->attributes & BARD_VM_ATTRIBUTE_TYPE_MASK) == BARD_VM_ATTRIBUTE_COMPOUND)
#define BardType_is_native( type )    (type->attributes & BARD_VM_ATTRIBUTE_NATIVE)
#define BardType_is_singleton( type ) (type->attributes & BARD_VM_ATTRIBUTE_SINGLETON)

#define BardMethod_is_native( m )    (m->attributes & BARD_VM_ATTRIBUTE_NATIVE)

#define BardVM_push_object(vm,obj)         (--vm->sp)->object = (BardObject*) obj
#define BardVM_push_c_string(vm,st)    BardVM_push_object( vm, BardString_create_with_c_string(vm,st) )
#define BardVM_push_c_string_with_length(vm,st,len)    BardVM_push_object( vm, BardString_create_with_c_string(vm,st,len) )
#define BardVM_push_real( vm, value )      (--vm->sp)->real = value
#define BardVM_push_integer( vm, value )   (--vm->sp)->integer = value
#define BardVM_push_character( vm, value ) (--vm->sp)->integer = value
#define BardVM_push_logical( vm, value )   (--vm->sp)->integer = value
#define BardVM_push_byte( vm, value )      (--vm->sp)->integer = value

#define BardVM_pop_discard( vm )              ++vm->sp
#define BardVM_pop_object( vm )     ((vm->sp++)->object)
#define BardVM_pop_string( vm )     ((BardString*)((vm->sp++)->object))
#define BardVM_pop_list( vm )       (BardArrayList*) ((vm->sp++)->object)
#define BardVM_pop_real( vm )       ((vm->sp++)->real)
#define BardVM_pop_integer( vm )    ((vm->sp++)->integer)
#define BardVM_pop_character( vm )  ((BardCharacter)((vm->sp++)->integer))
#define BardVM_pop_logical( vm )    ((BardInteger)((vm->sp++)->integer))
#define BardVM_pop_byte( vm )       ((BardByte)((vm->sp++)->integer))

#define BardVM_peek_object( vm )  (vm->sp->object)
#define BardVM_peek_string( vm )  ((BardString*)(vm->sp->object))
#define BardVM_peek_list( vm )    ((BardArrayList*) vm->sp->object)
#define BardVM_peek_real( vm )    (vm->sp->real)
#define BardVM_peek_integer( vm ) (vm->sp->integer)
#define BardVM_peek_logical( vm ) (vm->sp->integer)
#define BardVM_peek_byte( vm )     ((BardByte)(vm->sp->integer))

#define BardList_data(list)  (*((BardArray**)(((char*)list) + list->type->vm->BardList_data_offset)))
#define BardList_count(list) (*((BardInteger*)(((char*)list) + list->type->vm->BardList_count_offset)))
#define BardList_capacity(list) (BardList_data(list)->count)

#define Bard_collect_reference( obj_expression, cr_pending_objects ) \
  { \
    BardObject* cr_obj = (BardObject*) obj_expression; \
    if (cr_obj && cr_obj->size >= 0) \
    { \
      cr_obj->size = ~cr_obj->size; \
      cr_obj->next_reference = cr_pending_objects; \
      cr_pending_objects = cr_obj; \
    } \
  }

#define BARD_VM_CREATE_OP_WITH_ARG_N(op,n) ((n << 10) | op)

#define BARD_TRACE_TYPE_SKIP            0
#define BARD_TRACE_TYPE_STANDARD        1
#define BARD_TRACE_TYPE_REFERENCE_ARRAY 2
#define BARD_TRACE_TYPE_NATIVE_METHOD   3

#define BARD_INSERT_MESSAGE_LISTENER  -1
#define BARD_REPLACE_MESSAGE_LISTENER  0
#define BARD_APPEND_MESSAGE_LISTENER   1
#define BARD_DEFAULT_MESSAGE_LISTENER  2

struct BardVMMessageReader;
typedef void (*BardMessageListenerCallback)( struct BardVMMessageReader* reader, void* user_data );

#if defined(_WIN32)
// Windows-specific includes and defines
#  include "Windows.h"
#  define usleep(useconds) Sleep( (useconds)/1000 )
#  define PATH_MAX MAX_PATH
#  define snprintf sprintf_s
#endif // _WIN32

#endif
