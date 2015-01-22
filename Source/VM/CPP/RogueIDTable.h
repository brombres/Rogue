#pragma once

//=============================================================================
//  RogueIDTable.h
//
//  ---------------------------------------------------------------------------
//
//  Created 2015.01.18 by Abe Pralle
//
//  This is free and unencumbered software released into the public domain
//  under the terms of the UNLICENSE ( http://unlicense.org ).
//=============================================================================
#include <string.h>
#include "RogueAllocator.h"
#include "RogueSystemList.h"

//=============================================================================
//  RogueIDTableEntry
//=============================================================================
struct RogueIDTableEntry
{
  char*    name;
  int      hash_code;
  int      id;
  struct RogueIDTableEntry* next_entry;

  RogueIDTableEntry( const char* name, int hash_code, int id );
  ~RogueIDTableEntry();
};


//=============================================================================
//  RogueIDTable
//=============================================================================
struct RogueIDTable
{
  RogueIDTableEntry**                 bins;
  RogueSystemList<RogueIDTableEntry*> entry_list;
  int bin_count;
  int bin_mask;
  int count;

  RogueIDTable();
  ~RogueIDTable();

  int   calculate_hash_code( const char* st );
  void  clear();
  RogueIDTableEntry* get_entry( const char* name );
  const char* operator[]( int id );
  int         operator[]( const char* name );
};

extern RogueIDTable Rogue_id_table;
