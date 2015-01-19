//=============================================================================
//  RogueIDTable.cpp
//
//  ---------------------------------------------------------------------------
//
//  Created 2015.01.18 by Abe Pralle
//
//  This is free and unencumbered software released into the public domain
//  under the terms of the UNLICENSE ( http://unlicense.org ).
//=============================================================================
//=============================================================================
#include <string.h>
#include "RogueMM.h"
#include "RogueIDTable.h"

//=============================================================================
//  RogueIDTableEntry
//=============================================================================
RogueIDTableEntry::RogueIDTableEntry( const char* name, int hash_code, int id )
    : hash_code(hash_code), id(id), next_entry(NULL)
{
  int len = strlen( name );
  this->name = (char*) RogueMM_allocator.allocate( len+1 );
  strcpy( this->name, name );
}

RogueIDTableEntry::~RogueIDTableEntry()
{
  RogueMM_allocator.free( name, strlen(name)+1 );
}


//=============================================================================
//  RogueIDTable
//=============================================================================
RogueIDTable::RogueIDTable() : bins(NULL), count(0)
{
  bin_count = 64;
  bin_mask = bin_count - 1;

  bins = new RogueIDTableEntry*[ bin_count ];
  memset( bins, 0, bin_count * sizeof(RogueIDTableEntry*) );

  entry_list.add( NULL );
}

RogueIDTable::~RogueIDTable()
{
  clear();

  delete bins;
  bins = NULL;
  count = 0;
}

int RogueIDTable::calculate_hash_code( const char* st )
{
  int code = 0;
  unsigned char ch;
  --st;
  while ((ch = (unsigned char) *(++st))) code = ((code << 3) - code) + ch;  // code = code*7 + ch
  return code;
}

void RogueIDTable::clear()
{
  if (bins)
  {
    for (int i=bin_mask; i>=0; --i)
    {
      RogueIDTableEntry* entry = bins[i];
      if (entry)
      {
        bins[i] = NULL;
        while (entry)
        {
          RogueIDTableEntry* next_entry = entry->next_entry;
          delete entry;
          entry = next_entry;
        }
      }
    }
    count = 0;
  }
}

int RogueIDTable::get_id( const char* name )
{
  int hash_code = calculate_hash_code( name );
  RogueIDTableEntry* entry = bins[ hash_code & bin_mask ];

  while (entry)
  {
    if (entry->hash_code == hash_code && 0 == strcmp(entry->name,name))
    {
      return entry->id;
    }
    entry = entry->next_entry;
  }

  entry = new RogueIDTableEntry( name, hash_code, count+1 );
  entry->next_entry = bins[ hash_code & bin_mask ];
  bins[ hash_code & bin_mask ] = entry;
  ++count;

  entry_list.add( entry );

  return entry->id;
}

const char* RogueIDTable::operator[]( int id )
{
  if (id < 0 || id > count) return "";
  // id > count is correct here; count is one less than entry_list.count

  return entry_list[id]->name;
}

int RogueIDTable::operator[]( const char* name )
{
  return get_id( name );
}

RogueIDTable Rogue_id_table;

