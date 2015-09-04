//=============================================================================
//  RogueUTF8.c
//
//  2015.08.30 by Abe Pralle
//=============================================================================
#include "Rogue.h"

int RogueUTF8_decoded_count( const char* utf8_data, int utf8_count )
{
  // Returns the number of 16-bit Characters required to hold the decoded value
  // of the given UTF-8 string.
  if (utf8_count == -1) utf8_count = (int) strlen( utf8_data );

  const char* cur   = utf8_data - 1;
  const char* limit = utf8_data + utf8_count;

  int result_count = 0;
  while (++cur < limit)
  {
    ++result_count;
    int ch = *((unsigned char*)cur);
    if (ch >= 0x80)
    {
      if ((ch & 0xe0) == 0xc0) ++cur;
      else                     cur += 2;
    }
  }

  return result_count;
}

void RogueUTF8_decode( const char* utf8_data, RogueInteger utf8_count, RogueCharacter* dest_buffer )
{
  RogueByte*      src  = (RogueByte*)(utf8_data - 1);
  RogueCharacter* dest = dest_buffer - 1;

  int remaining_count = utf8_count;
  while (--remaining_count >= 0)
  {
    int ch = *(++src);
    if (ch >= 0x80)
    {
      if ((ch & 0xe0) == 0xc0)
      {
        // 110x xxxx  10xx xxxx
        ch = ((ch & 0x1f) << 6) | (*(++src) & 0x3f);
        --remaining_count;
      }
      else
      {
        // 1110 xxxx  10xx xxxx  10xx xxxx
        ch = ((ch & 0x1f) << 6) | (*(++src) & 0x3f);
        ch = (ch << 6) | (*(++src) & 0x3f);
        remaining_count -= 2;
      }
    }
    *(++dest) = (RogueCharacter) ch;
  }
}

RogueInteger RogueUTF8_encoded_count( RogueCharacter* characters, RogueInteger count )
{
  RogueInteger encoded_count = 0;
  --characters;
  while (--count >= 0)
  {
    RogueCharacter ch = *(++characters);
    if (ch <= 0x7f)       ++encoded_count;
    else if (ch <= 0x7ff) encoded_count += 2;
    else                  encoded_count += 3;
  }
  return encoded_count;
}

void RogueUTF8_encode( RogueCharacter* characters, RogueInteger count, char* dest_buffer )
{
  --dest_buffer;
  --characters;
  while (--count >= 0)
  {
    RogueCharacter ch = *(++characters);
    if (ch <= 0x7f)
    {
      // 0xxx xxxx
      *(++dest_buffer) = (char) ch;
    }
    else if (ch <= 0x7ff)
    {
      // 110x xxxx  10xx xxxx
      *(++dest_buffer) = (char) (0xC0 | ((ch >> 6) & 0x1F));
      *(++dest_buffer) = (char) (0x80 | (ch & 0x3F));
    }
    else
    {
      // 1110 xxxx  10xx xxxx  10xx xxxx
      *(++dest_buffer) = (char) (0xE0 | ((ch >> 12) & 0xF));
      *(++dest_buffer) = (char) (0x80 | ((ch >> 6) & 0x3F));
      *(++dest_buffer) = (char) (0x80 | (ch & 0x3F));
    }
  }
}
