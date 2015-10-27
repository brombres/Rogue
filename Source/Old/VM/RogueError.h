//=============================================================================
//  RogueError.h
//
//  2015.09.06 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_JUMP_BUFFER_H
#define ROGUE_JUMP_BUFFER_H

#include <setjmp.h>

#if defined(ROGUE_PLATFORM_MAC)
#  define ROGUE_SETJMP  _setjmp
#  define ROGUE_LONGJMP _longjmp
#else
#  define ROGUE_SETJMP  setjmp
#  define ROGUE_LONGJMP longjmp
#endif

#define ROGUE_TRY(vm) \
  { \
    RogueErrorHandler local_error_handler; \
    local_error_handler.previous_jump_buffer = vm->error_handler; \
    vm->error_handler = &local_error_handler; \
    int local_exception_code; \
    if ( !(local_exception_code = ROGUE_SETJMP(local_error_handler.info)) )

#define ROGUE_CATCH(vm) \
    else

#define ROGUE_END_TRY(vm) \
    vm->error_handler = local_error_handler.previous_jump_buffer; \
  }

#define ROGUE_THROW(vm,message) \
  if (message && ((char*)message)[0]) RogueStringBuilder_print_c_string( &vm->error_message_builder, message ); \
  ROGUE_LONGJMP( vm->error_handler->info, 1 )

//-----------------------------------------------------------------------------
//  Error Handler
//-----------------------------------------------------------------------------
struct RogueErrorHandler
{
  jmp_buf            info;
  RogueErrorHandler* previous_jump_buffer;
};


#endif // ROGUE_JUMP_BUFFER_H
