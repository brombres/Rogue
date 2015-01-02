#include "SuperBitmap.h"

#include <string.h>
#include <stdlib.h>

//=============================================================================
//  Blend and Filter Functions
//=============================================================================
SuperInteger SuperBlendFn_opaque( SuperInteger src, SuperInteger dest )
{
  return src;
}

SuperInteger SuperFilterFn_swap_red_and_blue( SuperBitmap* bitmap, int x, int y, SuperInteger color )
{
  return (color & 0xff00ff00) | ((color >> 16) & 0xff) | ((color << 16) & 0xff0000);
}

SuperInteger SuperFilterFn_premultiply_alpha( SuperBitmap* bitmap, int x, int y, SuperInteger color )
{
  int a = (color >> 24) & 255;
  int r = (((color >> 16) & 255) * a) / 255;
  int g = (((color >>  8) & 255) * a) / 255;
  int b = ((color & 255) * a) / 255;
  return (a << 24) | (r << 16) | (g << 8) | b;
}

//=============================================================================
//  Super Bitmaps 
//=============================================================================
SuperBitmap* SuperBitmap_create()
{
  return Super_allocate( SuperBitmap );
}

SuperBitmap* SuperBitmap_init( SuperBitmap* bitmap )
{
  memset( bitmap, 0, sizeof(SuperBitmap) );
  return bitmap;
}

SuperBitmap* SuperBitmap_destroy( SuperBitmap* bitmap )
{
  if ( !bitmap ) return 0;

  SuperBitmap_release_data( bitmap );
  SUPER_FREE( bitmap );
  return 0;
}

void SuperBitmap_adopt_data( SuperBitmap* bitmap, void* data, int w, int h, int bypp )
{
  int data_size = w * h * bypp;

  SuperBitmap_release_data( bitmap );

  bitmap->data = data;
  bitmap->data_size = data_size;
  bitmap->width = w;
  bitmap->height = h;
  bitmap->bypp = bypp;
  bitmap->owns_data = 1;
}

void SuperBitmap_allocate_data( SuperBitmap* bitmap, int w, int h, int bypp )
{
  int data_size = w * h * bypp;

  SuperBitmap_release_data( bitmap );

  bitmap->data = SUPER_ALLOCATE( data_size );
  bitmap->data_size = data_size;
  bitmap->width = w;
  bitmap->height = h;
  bitmap->bypp = bypp;
  bitmap->owns_data = 1;
}

void SuperBitmap_borrow_data( SuperBitmap* bitmap, void* data, int w, int h, int bypp )
{
  SuperBitmap_adopt_data( bitmap, data, w, h, bypp );
  bitmap->owns_data = 0;
}

void SuperBitmap_copy_data( SuperBitmap* bitmap, void* data, int w, int h, int bypp )
{
  int data_size = w * h * bypp;

  if ( !bitmap->owns_data )
  {
    bitmap->data = SUPER_ALLOCATE( data_size );
    bitmap->data_size = data_size;
    bitmap->owns_data = 1;
  }

  if (data_size > bitmap->data_size)
  {
    SuperBitmap_release_data( bitmap );
    bitmap->data = SUPER_ALLOCATE( data_size );
    bitmap->data_size = data_size;
    bitmap->owns_data = 1;
  }

  memcpy( bitmap->data, data, data_size );
  bitmap->width = w;
  bitmap->height = h;
  bitmap->bypp = bypp;
}

int SuperBitmap_release_data( SuperBitmap* bitmap )
{
  if ( !bitmap->owns_data )
  {
    bitmap->data      = 0;
    bitmap->data_size = 0;
    return 0;
  }

  if (bitmap->data)
  {
    SUPER_FREE( bitmap->data );
    bitmap->data = 0;
    bitmap->data_size = 0;
  }

  bitmap->owns_data = 0;

  return 1;
}

void SuperBitmap_blit( SuperBitmap* src_bitmap, SuperBitmap* dest_bitmap, int x, int y, SuperBlendFn fn )
{
  SuperBitmap_blit_area( src_bitmap, 0, 0, src_bitmap->width, src_bitmap->height, dest_bitmap, x, y, fn );
}

void SuperBitmap_blit_area( SuperBitmap* src_bitmap, int src_x1, int src_y1, int w, int h, SuperBitmap* dest_bitmap, int dest_x1, int dest_y1, SuperBlendFn fn )
{
  int bypp   = src_bitmap->bypp;
  int src_xlimit  = src_x1 + w;
  int src_ylimit  = src_y1 + h;
  int dest_xlimit = dest_x1 + w;
  int dest_ylimit = dest_y1 + h;
  int lines;
  int copy_count;
  int src_row_count;
  int dest_row_count;

  if ( !fn ) fn = SuperBlendFn_opaque;

  // clip
  if (src_x1 < 0)
  {
    int delta = -src_x1;
    src_x1    = 0;
    dest_x1  += delta;
  }
  if (src_y1 < 0)
  {
    int delta = -src_y1;
    src_y1    = 0;
    dest_y1  += delta;
  }
  if (src_xlimit > src_bitmap->width)
  {
    int delta    = src_xlimit - src_bitmap->width;
    src_xlimit  -= delta;
    dest_xlimit -= delta;
  }
  if (src_ylimit > src_bitmap->width)
  {
    int delta    = src_ylimit - src_bitmap->width;
    src_ylimit  -= delta;
    dest_ylimit -= delta;
  }

  if (dest_x1 < 0)
  {
    int delta = -dest_x1;
    dest_x1  = 0;
    src_x1   += delta;
  }
  if (dest_y1 < 0)
  {
    int delta = -dest_y1;
    dest_y1  = 0;
    src_y1   += delta;
  }
  if (dest_xlimit > dest_bitmap->width)
  {
    int delta = dest_xlimit - dest_bitmap->width;
    dest_xlimit -= delta;
    src_xlimit  -= delta;
  }
  if (dest_ylimit > dest_bitmap->height)
  {
    int delta = dest_ylimit - dest_bitmap->height;
    dest_ylimit -= delta;
    src_ylimit  -= delta;
  }

  lines = dest_ylimit - dest_y1;
  copy_count = (dest_xlimit - dest_x1);
  src_row_count = src_bitmap->width;
  dest_row_count = dest_bitmap->width;

  if (lines <= 0 || copy_count <= 0) return;

  switch (bypp)
  {
    case 4:
      {
        SuperInteger* src_start  = ((SuperInteger*)src_bitmap->data) + (src_y1 * src_bitmap->width + src_x1);
        SuperInteger* src_limit  = src_start + copy_count;
        SuperInteger* dest_start = ((SuperInteger*)dest_bitmap->data) + (dest_y1 * dest_bitmap->width + dest_x1);

        while (--lines >= 0)
        {
          SuperInteger* src  = src_start;
          SuperInteger* dest = dest_start;
          while (src < src_limit)
          {
            *dest = fn( *src, *dest );
            ++src;
            ++dest;
          }
          src_start  += src_row_count;
          src_limit  += src_row_count;
          dest_start += dest_row_count;
        }
      }
      break;

    case 2:
      {
        SuperCharacter* src_start  = ((SuperCharacter*)src_bitmap->data) + (src_y1 * src_bitmap->width + src_x1);
        SuperCharacter* src_limit  = src_start + copy_count;
        SuperCharacter* dest_start = ((SuperCharacter*)dest_bitmap->data) + (dest_y1 * dest_bitmap->width + dest_x1);

        while (--lines >= 0)
        {
          SuperCharacter* src  = src_start;
          SuperCharacter* dest = dest_start;
          while (src < src_limit)
          {
            *dest = (SuperCharacter) fn( *src, *dest );
            ++src;
            ++dest;
          }
          src_start  += src_row_count;
          src_limit  += src_row_count;
          dest_start += dest_row_count;
        }
      }
      break;

    case 1:
      {
        SuperByte* src_start  = ((SuperByte*)src_bitmap->data) + (src_y1 * src_bitmap->width + src_x1);
        SuperByte* src_limit  = src_start + copy_count;
        SuperByte* dest_start = ((SuperByte*)dest_bitmap->data) + (dest_y1 * dest_bitmap->width + dest_x1);

        while (--lines >= 0)
        {
          SuperByte* src  = src_start;
          SuperByte* dest = dest_start;
          while (src < src_limit)
          {
            *dest = (SuperByte) fn( *src, *dest );
            ++src;
            ++dest;
          }
          src_start  += src_row_count;
          src_limit  += src_row_count;
          dest_start += dest_row_count;
        }
      }
      break;
  }
}

void SuperBitmap_blit_column( SuperBitmap* bitmap, int src_x, int dest_x, SuperBlendFn fn )
{
  SuperBitmap_blit_area( bitmap, src_x, 0, 1, bitmap->height, bitmap, dest_x, 0, 0 );
}

void SuperBitmap_blit_row( SuperBitmap* bitmap, int src_y, int dest_y, SuperBlendFn fn )
{
  SuperBitmap_blit_area( bitmap, 0, src_y, bitmap->width, 1, bitmap, 0, dest_y, 0 );
}

void SuperBitmap_expand_to_power_of_two( SuperBitmap* bitmap )
{
  int w = 1;
  int h = 1;
  int original_w = bitmap->width;
  int original_h = bitmap->height;

  while (w < bitmap->width)  w <<= 1;
  while (h < bitmap->height) h <<= 1;

  SuperBitmap_crop( bitmap, w, h );
  SuperBitmap_extend_image_edges_to_surface_edges( bitmap, original_w, original_h );
}

void SuperBitmap_extend_image_edges_to_surface_edges( SuperBitmap* bitmap, int image_w, int image_h )
{
  int surface_w = bitmap->width;
  int surface_h = bitmap->height;
  int src, dest;

  if (image_w == surface_w && image_h == surface_h) return;
  if (image_w == 0 || image_h == 0) return;

  // Extend right edge
  src  = image_w - 1;
  dest = image_w;
  while (dest < surface_w)
  {
    SuperBitmap_blit_area( bitmap, src, 0, 1, image_h, bitmap, dest++, 0, 0 );
  }

  // Extend bottom edge
  src  = image_h - 1;
  dest = image_h;
  while (dest < surface_h) SuperBitmap_blit_row( bitmap, src, dest++, 0 );
}

void SuperBitmap_crop( SuperBitmap* bitmap, int w, int h )
{
  if (w == bitmap->width && h == bitmap->height) return;

  if (w < bitmap->width && w*h <= bitmap->width * bitmap->height)
  {
    // safe to copy in place
    SuperBitmap cropped;
    SuperBitmap_init( &cropped );

    SuperBitmap_promote( bitmap );

    SuperBitmap_adopt_data( &cropped, bitmap->data, w, h, bitmap->bypp );
    SuperBitmap_blit( bitmap, &cropped, 0, 0, 0 );
    bitmap->width = w;
    bitmap->height = h;
  }
  else
  {
    SuperBitmap cropped;
    SuperBitmap_init( &cropped );

    SuperBitmap_allocate_data( &cropped, w, h, 4 );

    SuperBitmap_blit( bitmap, &cropped, 0, 0, 0 );
    SuperBitmap_release_data( bitmap );
    bitmap->data = cropped.data;
    bitmap->data_size = cropped.data_size;
    bitmap->width  = w;
    bitmap->height = h;
    bitmap->owns_data = 1;
  }
}

void SuperBitmap_filter( SuperBitmap* bitmap, SuperFilterFn fn )
{
  SuperBitmap_filter_area( bitmap, 0, 0, bitmap->width, bitmap->height, fn );
}

void SuperBitmap_filter_area( SuperBitmap* bitmap, int x, int y, int w, int h, SuperFilterFn fn )
{
  int lines;
  int columns;

  int bitmap_w = bitmap->width;
  int bitmap_h = bitmap->height;

  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (w > bitmap_w) w = bitmap_w;
  if (h > bitmap_h) h = bitmap_h;

  lines = h - y;
  columns = w - x;

  if (lines <= 0 || columns <= 0 || !fn ) return;

  switch (bitmap->bypp)
  {
    case 4:
      {
        int cur_y = y;
        SuperInteger* row_start = ((SuperInteger*)(bitmap->data)) + (y*bitmap_w) + x;
        while (--lines >= 0)
        {
          int cur_x = x;
          SuperInteger* cur = row_start;
          int count = columns;
          while (--count >= 0)
          {
            *cur = fn( bitmap, cur_x, cur_y, *cur );
            ++cur_x;
            ++cur;
          }
          ++cur_y;
          row_start += bitmap_w;
        }
      }
      break;

    case 2:
      {
        int cur_y = y;
        SuperCharacter* row_start = ((SuperCharacter*)(bitmap->data)) + (y*bitmap_w) + x;
        while (--lines >= 0)
        {
          int cur_x = x;
          SuperCharacter* cur = row_start;
          int count = columns;
          while (--count >= 0)
          {
            *cur = (SuperCharacter) fn( bitmap, cur_x, cur_y, *cur );
            ++cur_x;
            ++cur;
          }
          ++cur_y;
          row_start += bitmap_w;
        }
      }
      break;

    case 1:
      {
        int cur_y = y;
        SuperByte* row_start = ((SuperByte*)(bitmap->data)) + (y*bitmap_w) + x;
        while (--lines >= 0)
        {
          int cur_x = x;
          SuperByte* cur = row_start;
          int count = columns;
          while (--count >= 0)
          {
            *cur = (SuperByte) fn( bitmap, cur_x, cur_y, *cur );
            ++cur_x;
            ++cur;
          }
          ++cur_y;
          row_start += bitmap_w;
        }
      }
      break;
  }
}

void SuperBitmap_promote( SuperBitmap* bitmap )
{
  int   size;
  void* data;

  if (bitmap->owns_data) return;

  size = bitmap->width * bitmap->height * bitmap->bypp;
  data = SUPER_ALLOCATE( size );

  memcpy( data, bitmap->data, size );

  bitmap->data = data;
  bitmap->data_size = size;
  bitmap->owns_data = 1;
}

void SuperBitmap_swap_red_and_blue( SuperBitmap* bitmap )
{
  SuperBitmap_filter( bitmap, SuperFilterFn_swap_red_and_blue );
}

