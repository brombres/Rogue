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

#define ROGUE_STRING(vm,c_string) RogueString_create_from_c_string(vm,c_string)

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
typedef struct RogueAllocation        RogueAllocation;
typedef struct RogueAllocator         RogueAllocator;
typedef struct RogueArray             RogueArray;
typedef struct RogueCmd               RogueCmd;
typedef struct RogueCmdBinaryOp       RogueCmdBinaryOp;
typedef struct RogueCmdLiteralInteger RogueCmdLiteralInteger;
typedef struct RogueCmdType           RogueCmdType;
typedef struct RogueErrorHandler      RogueErrorHandler;
typedef struct RogueList              RogueList;
typedef struct RogueObject            RogueObject;
typedef struct RogueParsePosition     RogueParsePosition;
typedef struct RogueParseReader       RogueParseReader;
typedef struct RogueType              RogueType;
typedef struct RogueString            RogueString;
typedef struct RogueStringBuilder     RogueStringBuilder;
typedef struct RogueTable             RogueTable;
typedef struct RogueTableEntry        RogueTableEntry;
typedef struct RogueTokenizer         RogueTokenizer;
typedef struct RogueVM                RogueVM;
typedef struct RogueVMList            RogueVMList;
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

#include "RogueError.h"
#include "RogueStringBuilder.h"
#include "RogueTokenType.h"
#include "RogueUTF8.h"
#include "RogueAllocation.h"
#include "RogueAllocator.h"
#include "RogueObject.h"
#include "RogueString.h"
#include "RogueType.h"
#include "RogueETC.h"
#include "RogueTokenizer.h"
#include "RogueCmd.h"
#include "RogueParseReader.h"
#include "RogueVM.h"
#include "RogueVMList.h"
#include "RogueArray.h"
#include "RogueList.h"
#include "RogueTable.h"

#endif // ROGUE_H
