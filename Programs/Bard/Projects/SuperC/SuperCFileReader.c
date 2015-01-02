#include "SuperCFileReader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
//  SuperCFileReader
//=============================================================================
SuperCFileReader* SuperCFileReader_create( SuperC* super_c, const char* filepath )
{
  FILE* infile;
  SuperCFileReader* reader = SuperC_allocate( super_c, sizeof(SuperCFileReader) );
  memset( reader, 0, sizeof(SuperCFileReader) );

  reader->filepath = SuperC_clone_c_string( super_c, filepath );

  infile = fopen( filepath, "rb" );
  if (infile)
  {
    unsigned char*  src;
    unsigned short* dest;
    int count;

    reader->exists = 1;

    fseek( infile, 0, SEEK_END );
    count = (int) ftell(infile);
    reader->count = count;
    fseek( infile, 0, SEEK_SET );

    // Allocate buffer of 16-bit characters
    reader->characters = (unsigned short*) SuperC_allocate( super_c, reader->count * 2 );

    // Read 8-bit bytes into last half of buffer
    dest = reader->characters;
    src = (((unsigned char*)dest) + reader->count);
    fread( src, 1, reader->count, infile );
    --src;
    --dest;

    // Copy 8-bit bytes from middle to 16-bit characters at beginning,
    // filtering and translating as necessary. 
    while (--count >= 0)
    {
      unsigned char b = *(++src);
      if (b >= 32 && b <= 126)
      {
        *(++dest) = b;
      }
      else
      {
        switch (b)
        {
          case '\t':
            *(++dest) = ' ';
            break;

          case '\n':
            *(++dest) = '\n';
            break;
        }
      }
    }
  }

  return reader;
}

SuperCFileReader* SuperCFileReader_destroy( SuperC* super_c, SuperCFileReader* reader )
{
  if (reader)
  {
    SuperC_free( super_c, reader->filepath );
    SuperC_free( super_c, reader->characters );
    return SuperC_free( super_c, reader );
  }
  return 0;
}
