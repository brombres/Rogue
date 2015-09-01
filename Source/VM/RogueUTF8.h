//=============================================================================
//  RogueUTF8.h
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#pragma once
#ifndef ROGUE_UTF8_H
#define ROGUE_UTF8_H

#include "Rogue.h"

int  RogueUTF8_decoded_count( const char* utf8_data, int utf8_count );
void RogueUTF8_decode( const char* utf8_data, RogueCharacter* dest_buffer, int decoded_count );

#endif // ROGUE_UTF8_H
