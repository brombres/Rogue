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
void RogueUTF8_decode( const char* utf8_data, RogueInteger utf8_count, RogueCharacter* dest_buffer );

RogueInteger RogueUTF8_encoded_count( RogueCharacter* characters, RogueInteger count );
void         RogueUTF8_encode( RogueCharacter* characters, RogueInteger count, char* dest_buffer );

#endif // ROGUE_UTF8_H
