#ifndef SUPER_BITMAP_H
#define SUPER_BITMAP_H

#include "Super.h"

SUPER_BEGIN_HEADER

// ADOPT:  Bitmap uses existing data and is responsible for its release.
// BORROW: Bitmap uses existing data and is not responsible for its release.
// COPY:   Bitmap copies existing data and frees the copy.  It is not responsible for the original.
#define SUPER_BITMAP_ADOPT_DATA  -1
#define SUPER_BITMAP_BORROW_DATA  0
#define SUPER_BITMAP_COPY_DATA    1
#define SUPER_BITMAP_ALLOCATE     2

typedef struct SuperBitmap
{
  int   width;
  int   height;

  int   bypp;       // BYtes Per Pixel

  int   owns_data;

  void* data;
  int   data_size;
} SuperBitmap;

typedef SuperInteger (*SuperBlendFn)( SuperInteger src, SuperInteger dest );
typedef SuperInteger (*SuperFilterFn)( SuperBitmap* bitmap, int x, int y, SuperInteger color );

SuperBitmap* SuperBitmap_create();
SuperBitmap* SuperBitmap_init( SuperBitmap* bitmap );
SuperBitmap* SuperBitmap_destroy( SuperBitmap* bitmap );

void SuperBitmap_adopt_data( SuperBitmap* bitmap, void* data, int w, int h, int bypp );
void SuperBitmap_allocate_data( SuperBitmap* bitmap, int w, int h, int bypp );
void SuperBitmap_borrow_data( SuperBitmap* bitmap, void* data, int w, int h, int bypp );
void SuperBitmap_copy_data( SuperBitmap* bitmap, void* data, int w, int h, int bypp );

int  SuperBitmap_release_data( SuperBitmap* bitmap );

void SuperBitmap_blit( SuperBitmap* src_bitmap, SuperBitmap* dest_bitmap, int x, int y, SuperBlendFn fn );
void SuperBitmap_blit_area( SuperBitmap* src_bitmap, int sx, int sy, int w, int h, SuperBitmap* dest_bitmap, int dx, int dy, SuperBlendFn fn );
void SuperBitmap_blit_column( SuperBitmap* bitmap, int src_x, int dest_x, SuperBlendFn fn );
void SuperBitmap_blit_row( SuperBitmap* bitmap, int src_y, int dest_y, SuperBlendFn fn );
void SuperBitmap_crop( SuperBitmap* bitmap, int new_w, int new_h );
void SuperBitmap_expand_to_power_of_two( SuperBitmap* bitmap );
void SuperBitmap_extend_image_edges_to_surface_edges( SuperBitmap* bitmap, int w, int h );
void SuperBitmap_filter( SuperBitmap* bitmap, SuperFilterFn fn );
void SuperBitmap_filter_area( SuperBitmap* bitmap, int x, int y, int w, int h, SuperFilterFn fn );
void SuperBitmap_promote( SuperBitmap* bitmap );
void SuperBitmap_swap_red_and_blue( SuperBitmap* bitmap );

//=============================================================================
//  Blend and Filter Functions
//=============================================================================

SuperInteger SuperBlendFn_opaque( SuperInteger src, SuperInteger dest );
SuperInteger SuperFilterFn_swap_red_and_blue( SuperBitmap* bitmap, int x, int y, SuperInteger color );
SuperInteger SuperFilterFn_premultiply_alpha( SuperBitmap* bitmap, int x, int y, SuperInteger color );

SUPER_END_HEADER

#endif // SUPER_BITMAP_H
