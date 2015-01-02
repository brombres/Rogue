#ifndef BARD_METHOD_H
#define BARD_METHOD_H

#include "BardXC.h"

typedef struct BardVMExceptionHandlerCatchInfo
{
  BardType*    catch_type;
  BardXCInteger* handler_ip;
  int local_slot_index;
} BardVMExceptionHandlerCatchInfo;

typedef struct BardVMExceptionHandlerInfo
{
  BardXCInteger* ip_start;
  BardXCInteger* ip_limit;
  int                            catch_count;
  BardVMExceptionHandlerCatchInfo* catches;
} BardVMExceptionHandlerInfo;

typedef struct BardMethod
{
  BardType*  type_context;
  char*      name;           // Does not need to be freed - comes from VM's session id_list.
  char*      signature; // May be null
  int        signature_hash_code;
  BardType** local_types;
  BardType*  return_type;

  char**     tags;
  int        tag_count;

  int        attributes;
  int        global_index;

  int        parameter_count;
  int        local_count;       // # of locals vars

  int        parameter_slot_count;
  int        local_slot_count;   // total # of local stack slots used for local vars
  int        standard_return_slot_delta;  // 0 for methods with return values, # of context slots to pop for nil methods

  int*       ip;  // as actual pointer
  int        ip_start;  // zero-based
  int        ip_limit;  // zero-based
  int        line_info_index;

  BardVMNativeMethod native_method;

  int                       exception_handler_count;
  BardVMExceptionHandlerInfo* exception_handlers;

  BardXCInteger cycle_count; // used when profiling

} BardMethod;

BardMethod* BardMethod_create( int global_index, BardType* type_context, char* name, int parameter_count, BardType* return_type, int local_count );
void        BardMethod_destroy( BardMethod* m );

//char*       BardMethod_signature( BardMethod* m );
char*       BardMethod_signature( BardMethod* m );

//void        BardMethod_create_signature_if_necessary( BardMethod* m );
void        BardMethod_create_signature_if_necessary( BardMethod* m );

int         BardMethod_signature_count( BardMethod* m );
void        BardMethod_compose_signature( BardMethod* m, char* buffer );

void        BardMethod_organize( BardMethod* m );

#endif // BARD_METHOD_H
