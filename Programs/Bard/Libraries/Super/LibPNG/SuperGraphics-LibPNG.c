#include "SuperGraphics.h"

#include <stdio.h>
#include <stdlib.h>
#include "png.h"

//int  SuperGraphics_create_texture( int w, int h, int* data );

//-----------------------------------------------------------------------------
//  LibPNG interface code based on www.libpng.org/pub/png/book/chapter13.html
//-----------------------------------------------------------------------------
static png_structp png_ptr;
static png_infop   info_ptr;
static int png_bit_depth;
static int png_color_type;

static void* Super_load_png_from_file( FILE* fp, int* width_ptr, int* height_ptr, int* bpp_ptr )
{
  unsigned char sig[8];
  int width, height;
  int        i;
  int        bytes_per_row;
  int        bpp;
  int        final_bpp;
  png_bytep* row_pointers;
  SuperByte* data;

  fread( sig, 1, 8, fp );
  if ( !png_check_sig(sig, 8) ) return NULL;

  png_ptr = png_create_read_struct( PNG_LIBPNG_VER_STRING, NULL, NULL, NULL );
  if (!png_ptr) return NULL;   // out of memory

  info_ptr = png_create_info_struct( png_ptr );
  if (!info_ptr) 
  {
    png_destroy_read_struct( &png_ptr, NULL, NULL );
    return NULL;   // out of memory
  }

  if (setjmp(png_jmpbuf(png_ptr)))
  {
    png_destroy_read_struct( &png_ptr, &info_ptr, NULL );
    return NULL;
  }

  png_init_io( png_ptr, fp );
  png_set_sig_bytes( png_ptr, 8 );
  png_read_info( png_ptr, info_ptr );

  png_get_IHDR( png_ptr, info_ptr, (png_uint_32*) &width, (png_uint_32*) &height, &png_bit_depth, &png_color_type, NULL, NULL, NULL );
  *width_ptr  = width;
  *height_ptr = height;

  row_pointers = (png_bytep*) SUPER_ALLOCATE( height * sizeof(png_bytep) );

  png_set_expand( png_ptr );
  png_set_strip_16( png_ptr );
  png_set_gray_to_rgb( png_ptr );

  png_read_update_info( png_ptr, info_ptr );

  bytes_per_row = (int) png_get_rowbytes(png_ptr, info_ptr);

  if (width) bpp = (bytes_per_row*8)/width;
  else       bpp = 0;

  //*channel_count = (int)png_get_channels( png_ptr, info_ptr );

  final_bpp = bpp;
  if (bpp == 24) final_bpp = 32;
  *bpp_ptr = final_bpp;

  if (bpp > 32 || (data = (SuperByte*)SUPER_ALLOCATE(((final_bpp/8)*width)*height)) == NULL)
  {
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    return NULL;
  }

  for (i = 0;  i < height;  ++i)
  {
    row_pointers[i] = data + i*bytes_per_row;
  }

  png_read_image( png_ptr, row_pointers );
  SUPER_FREE( row_pointers );

  png_read_end( png_ptr, NULL );

  png_destroy_read_struct( &png_ptr, &info_ptr, NULL );

  if (bpp == 24)
  {
    // Convert 24 bit BGR to 32 bit ARGB in-place
    int count = width * height;
    SuperByte* src  = data + count*3 - 3;
    SuperByte* dest = data + count*4 - 4;
    while (--count >= 0)
    {
      // Use a temp var to avoid overwriting values for pixel 0
      int b = src[2];
      dest[3] = 255;     // A
      dest[2] = src[0];  // R
      dest[1] = src[1];  // G
      dest[0] = b;       // B
      dest -= 4;
      src -= 3;
    }
  }
  else if (bpp == 32)
  {
    // Convert 32 bit ABGR to 32 bit ARGB in-place
    int count = width * height;
    SuperByte* src  = data + count*4 - 4;
    while (--count >= 0)
    {
      int temp = src[0];
      src[0] = src[2];
      src[2] = temp;
      src -= 4;
    }
  }
  
  return data;
}


//-----------------------------------------------------------------------------
//  SuperGraphics
//-----------------------------------------------------------------------------
void* SuperGraphics_load_pixel_data_from_png_file( const char* filepath, int* width_ptr, int* height_ptr, int* bits_per_pixel )
{
  void* data;
  FILE* fp = fopen( filepath, "rb" );

  if ( !fp ) return 0;

  data = (SuperInteger*) Super_load_png_from_file( fp, width_ptr, height_ptr, bits_per_pixel );
  fclose( fp );

  if ( !data ) return 0;

  if (*bits_per_pixel != 32)
  {
    SUPER_FREE( data );
    return 0;
  }

  return data;
}

