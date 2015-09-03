//=============================================================================
//  Rogue.h
//
//  2015.08.29 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_H
#define ROGUE_H

#if !defined(ROGUE_STACK_SIZE)
#  define ROGUE_STACK_SIZE 4096
#endif

#if defined(_WIN32)
#  define ROGUE_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
#  define ROGUE_PLATFORM_MAC 1
#else
#  define ROGUE_PLATFORM_GENERIC 1
#endif

#if defined(ROGUE_PLATFORM_WINDOWS)
#  include <windows.h>
#else
#  include <stdint.h>
#endif

#include <stdio.h>

#include <stdlib.h>
#include <string.h>

#ifdef ROGUE_PLATFORM_ANDROID
#  include <android/log.h>
#  define ROGUE_LOG(...) __android_log_print( ANDROID_LOG_ERROR, "Rogue", __VA_ARGS__ )
#else
#  define ROGUE_LOG(...) printf( __VA_ARGS__ )
#endif

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
    JumpBuffer local_jump_buffer; \
    local_jump_buffer.previous_jump_buffer = vm->jump_buffer; \
    vm->jump_buffer = &local_jump_buffer; \
    int local_exception_code; \
    if ( !(local_exception_code = ROGUE_SETJMP(local_jump_buffer.info)) ) \
    { \

#define ROGUE_CATCH_ANY \
    } else {

#define ROGUE_CATCH(err) \
    } else { \
      err = local_exception_code;

#define ROGUE_END_TRY(vm) \
    } \
    vm->jump_buffer = local_jump_buffer.previous_jump_buffer; \
  }

#define ROGUE_THROW(vm,code) ROGUE_LONGJMP( vm->jump_buffer->info, code )

#if defined(ROGUE_PLATFORM_WINDOWS)
  typedef double           RogueReal;
  typedef float            RogueFloat;
  typedef __int64          RogueLong;
  typedef __int32          RogueInteger;
  typedef unsigned __int16 RogueCharacter;
  typedef unsigned char    RogueByte;
  typedef RogueInteger     RogueLogical;
#else
  typedef double           RogueReal;
  typedef float            RogueFloat;
  typedef int64_t          RogueLong;
  typedef int32_t          RogueInteger;
  typedef uint16_t         RogueCharacter;
  typedef uint8_t          RogueByte;
  typedef RogueInteger     RogueLogical;
#endif

//-----------------------------------------------------------------------------
//  Jump Buffer
//-----------------------------------------------------------------------------
//struct JumpBuffer
//{
//  jmp_buf     info;
//  JumpBuffer* previous_jump_buffer;
//};


//-----------------------------------------------------------------------------
//  Allocator
//-----------------------------------------------------------------------------
#ifndef ROGUE_MM_PAGE_SIZE
// 4k; should be a multiple of 256 if redefined
#  define ROGUE_MM_PAGE_SIZE (4*1024)
#endif

// 0 = large allocations, 1..4 = block sizes 64, 128, 192, 256
#ifndef ROGUE_MM_SLOT_COUNT
#  define ROGUE_MM_SLOT_COUNT 5
#endif

// 2^6 = 64
#ifndef ROGUE_MM_GRANULARITY_BITS
#  define ROGUE_MM_GRANULARITY_BITS 6
#endif

// Block sizes increase by 64 bytes per slot
#ifndef ROGUE_MM_GRANULARITY_SIZE
#  define ROGUE_MM_GRANULARITY_SIZE (1 << ROGUE_MM_GRANULARITY_BITS)
#endif

// 63
#ifndef ROGUE_MM_GRANULARITY_MASK
#  define ROGUE_MM_GRANULARITY_MASK (ROGUE_MM_GRANULARITY_SIZE - 1)
#endif

// Small allocation limit is 256 bytes - afterwards objects are allocated
// from the system.
#ifndef ROGUE_MM_SMALL_ALLOCATION_SIZE_LIMIT
#  define ROGUE_MM_SMALL_ALLOCATION_SIZE_LIMIT  ((ROGUE_MM_SLOT_COUNT-1) << ROGUE_MM_GRANULARITY_BITS)
#endif

//-----------------------------------------------------------------------------
//  FORWARD DECLARATIONS
//-----------------------------------------------------------------------------
typedef struct RogueArray         RogueArray;
typedef struct RogueList          RogueList;
typedef struct RogueObject        RogueObject;
typedef struct RogueType          RogueType;
typedef struct RogueString        RogueString;
typedef struct RogueStringBuilder RogueStringBuilder;
typedef struct RogueTable         RogueTable;
typedef struct RogueTableEntry    RogueTableEntry;
typedef struct RogueVM            RogueVM;
//struct Object;
//struct ParseReader;
//struct Scope;
//struct StringBuilder;
//struct String;
//struct Tokenizer;
//

//-----------------------------------------------------------------------------
//  EXCEPTION CODES
//-----------------------------------------------------------------------------
//enum
//{
//  COMPILE_ERROR   = 1,
//  RUNTIME_ERROR,
//};

//-----------------------------------------------------------------------------
//  EXECUTION CODES
//-----------------------------------------------------------------------------
//enum Status
//{
//  STATUS_NORMAL,
//};
//

#include "RogueUTF8.h"
#include "RogueAllocator.h"
#include "RogueObject.h"
#include "RogueString.h"
#include "RogueType.h"
#include "RogueETC.h"
#include "RogueReader.h"
#include "RogueCmd.h"
#include "RogueVM.h"
#include "RogueArray.h"
#include "RogueList.h"
#include "RogueStringBuilder.h"
#include "RogueTable.h"

#endif // ROGUE_H
