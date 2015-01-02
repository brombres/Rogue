#ifndef SUPER_C_FILE_READER_H
#define SUPER_C_FILE_READER_H

#include "SuperC.h"

SUPER_C_BEGIN_HEADER

//=============================================================================
//  SuperCFileReader
//=============================================================================
typedef struct SuperCFileReader
{
  char*           filepath;
  unsigned short* characters;
  int             count;
  int             position;
  int             exists;
} SuperCFileReader;

SuperCFileReader* SuperCFileReader_create( SuperC* super_c, const char* filepath );
SuperCFileReader* SuperCFileReader_destroy( SuperC* super_c, SuperCFileReader* reader );

SUPER_C_END_HEADER

#endif // SUPER_C_FILE_READER_H
