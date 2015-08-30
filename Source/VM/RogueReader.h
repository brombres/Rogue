//=============================================================================
//  RogueReader.h
//
//  2015.08.29 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_READER_H
#define ROGUE_READER_H

//-----------------------------------------------------------------------------
//  RogueReaderPosition
//-----------------------------------------------------------------------------
typedef struct RogueReaderPosition
{
  int index;
  int line;
  int column;
} RogueReaderPosition;

//-----------------------------------------------------------------------------
//  RogueReader
//-----------------------------------------------------------------------------
typedef struct RogueReader
{
  RogueReaderPosition position;
} RogueReader;

RogueReader* RogueReader_create( const char* filepath );

#endif // ROGUE_READER_H
