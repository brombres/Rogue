#ifndef SUPER_GRAPHICS_H
#define SUPER_GRAPHICS_H

#include "Super.h"

SUPER_BEGIN_HEADER

#define SUPER_MAX_BUFFERED_VERTICES (512*3)
#define SUPER_DRAW_TEXTURED_TRIANGLES 1
#define SUPER_DRAW_SOLID_TRIANGLES    2
#define SUPER_DRAW_LINES              3
#define SUPER_DRAW_POINTS             4

#define SUPER_CLEAR_COLOR   1
#define SUPER_CLEAR_DEPTH   2
#define SUPER_CLEAR_STENCIL 4

#define SUPER_TEXTURE_FORMAT_RGBA_32             0
#define SUPER_TEXTURE_FORMAT_PREMULTIPLY_ALPHA 256

#define SUPER_RENDER_BLEND                   1
#define SUPER_RENDER_ADJUST_VERTEX_COLORS    2
#define SUPER_RENDER_TEXTURE_WRAP           12
#define SUPER_RENDER_POINT_FILTER           16
#define SUPER_RENDER_OPTION_MASK          0xFF

// src_blend  is render_mode >> 8
// dest_blend is render_mode >> 12
#define SUPER_BLEND_ZERO                0x00
#define SUPER_BLEND_ONE                 0x01
#define SUPER_BLEND_SRC_ALPHA           0x02
#define SUPER_BLEND_INVERSE_SRC_ALPHA   0x03
#define SUPER_BLEND_MASK                0x0f

#define SUPER_VERTEX_COLOR_MODE_NONE     0x000000
#define SUPER_VERTEX_COLOR_MODE_MULTIPLY 0x010000
#define SUPER_VERTEX_COLOR_MODE_ADD      0x020000
#define SUPER_VERTEX_COLOR_MODE_FILL     0x030000
#define SUPER_VERTEX_COLOR_MODE_MASK     0xff0000

#define SuperGraphics_create_render_mode( options, src_blend, dest_blend, vertex_color_mode ) (options | (src_blend<<8) | (dest_blend<<12) | vertex_color_mode)

struct SuperUIWindowInfo;

//=============================================================================
//  SuperMatrix
//=============================================================================
typedef struct SuperMatrix
{
  double at[16];
} SuperMatrix;

SuperMatrix* SuperMatrix_copy( SuperMatrix* src, SuperMatrix* dest );
SuperMatrix* SuperMatrix_identity( SuperMatrix* m );
SuperMatrix* SuperMatrix_multiply( SuperMatrix* a, SuperMatrix* b, SuperMatrix* c );
SuperMatrix* SuperMatrix_orthographic( SuperMatrix* m, int width, int height, double depth );
SuperReal32* SuperMatrix_to_real32( SuperMatrix* m, SuperReal32* dest );
SuperMatrix* SuperMatrix_zeros( SuperMatrix* m );

//=============================================================================
//  SuperGraphicsVertex
//=============================================================================
typedef struct SuperGraphicsVertex
{
  SuperReal32  x, y, z;
  SuperReal32  u, v;
  SuperInteger color;
} SuperGraphicsVertex;


//=============================================================================
//  SuperGraphics
//=============================================================================
typedef struct SuperGraphicsDrawBuffer
{
  // SuperGraphicsDrawBuffer is a renderer-independent vertex buffer and render 
  // state that gets translated into renderer-specific data when
  // SuperGraphics_flush() is called.
  SuperGraphicsVertex vertices[ SUPER_MAX_BUFFERED_VERTICES ];
  int vertex_count;

  int draw_mode; // DRAW_TEXTURED_TRIANGLES, etc.

  int render_mode;  // packed vertex_color_mode[23:16], texture_mode[15:8], dest_blend[7:4], src_blend[3:0]

  int display_id;
  int display_width;
  int display_height;

  int   texture_id;
  void* texture;

  SuperMatrix projection_transform;

  // This might be faster with SuperInteger - worth profiling sometime.
  SuperByte alpha_multiplication_lookup[65536];
} SuperGraphicsDrawBuffer;

extern SuperGraphicsDrawBuffer SuperGraphics;

void  SuperGraphicsDrawBuffer_init();

void  SuperGraphics_configure();
void  SuperGraphics_retire();

void  SuperGraphics_begin_draw( int display_id, int display_width, int display_height );

void  SuperGraphics_clear( int flags, int color, double depth, int stencil );
void  SuperGraphics_clip( int x, int y, int w, int h );
int   SuperGraphics_create_texture( void* data, int w, int h, int flags );
int   SuperGraphics_create_texture_from_file( const char* filepath, int flags );
void  SuperGraphics_destroy_texture( int texture_id );
void  SuperGraphics_draw_line( double x1, double y1, double x2, double y2, int color );

void  SuperGraphics_end_draw();

void  SuperGraphics_fill_colored_triangle( double x1, double y1, int color1, double x2, double y2, int color2, double x3, double y3, int color3 );
void  SuperGraphics_fill_textured_box( double x, double y, double w, double h, double u1, double v1, double u2, double v2, int color, int texture_id );
void  SuperGraphics_fill_textured_triangle( double x1, double y1, double u1, double v1, int color1,
                                            double x2, double y2, double u2, double v2, int color2,
                                            double x3, double y3, double u3, double v3, int color3,
                                            int texture_id );
int   SuperGraphics_get_texture_info( int texture_id, int* image_width_ptr, int* image_height_ptr, 
                                      int* texture_width_ptr, int* texture_height_ptr, int* bits_per_pixel_ptr );
void* SuperGraphics_load_pixel_data_from_png_file( const char* filepath, int* width_ptr, int* height_ptr, int* bits_per_pixel );

void  SuperGraphics_flush();

void  SuperGraphics_premultiply_alpha( SuperInteger* data, int count );

void  SuperGraphics_set_projection_transform( SuperMatrix* m );
void  SuperGraphics_set_render_mode( int render_mode );
void  SuperGraphics_set_texture( int texture_id, int w, int h, void* data, int flags );

void  SuperGraphics_swap_red_and_blue( SuperInteger* data, int count );

SUPER_END_HEADER

#endif // SUPER_GRAPHICS_H

