#include "SuperGraphics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
//  SuperMatrix
//=============================================================================
SuperMatrix* SuperMatrix_copy( SuperMatrix* src, SuperMatrix* dest )
{
  memcpy( dest, src, sizeof(SuperMatrix) );
  return dest;
}

SuperMatrix* SuperMatrix_identity( SuperMatrix* m )
{
  memset( m->at, 0, sizeof(void*) * 16 );
  m->at[0]  = 1;
  m->at[5]  = 1;
  m->at[10] = 1;
  m->at[15] = 1;
  return m;
}

SuperMatrix* SuperMatrix_multiply( SuperMatrix* a, SuperMatrix* b, SuperMatrix* c )
{
  c->at[0] = a->at[0]  * b->at[0]
           + a->at[4]  * b->at[1]
           + a->at[8]  * b->at[2]
           + a->at[12] * b->at[3];

  c->at[1] = a->at[1]  * b->at[0]
           + a->at[5]  * b->at[1]
           + a->at[9]  * b->at[2]
           + a->at[13] * b->at[3];

  c->at[2] = a->at[2]  * b->at[0]
           + a->at[6]  * b->at[1]
           + a->at[10] * b->at[2]
           + a->at[14] * b->at[3];

  c->at[3] = a->at[3]  * b->at[0]
           + a->at[7]  * b->at[1]
           + a->at[11] * b->at[2]
           + a->at[15] * b->at[3];

  c->at[4] = a->at[0]  * b->at[4]
           + a->at[4]  * b->at[5]
           + a->at[8]  * b->at[6]
           + a->at[12] * b->at[7];

  c->at[5] = a->at[1]  * b->at[4]
           + a->at[5]  * b->at[5]
           + a->at[9]  * b->at[6]
           + a->at[13] * b->at[7];

  c->at[6] = a->at[2]  * b->at[4]
           + a->at[6]  * b->at[5]
           + a->at[10] * b->at[6]
           + a->at[14] * b->at[7];

  c->at[7] = a->at[3]  * b->at[4]
           + a->at[7]  * b->at[5]
           + a->at[11] * b->at[6]
           + a->at[15] * b->at[7];

  c->at[8] = a->at[0]  * b->at[8]
           + a->at[4]  * b->at[9]
           + a->at[8]  * b->at[10]
           + a->at[12] * b->at[11];

  c->at[9] = a->at[1]  * b->at[8]
           + a->at[5]  * b->at[9]
           + a->at[9]  * b->at[10]
           + a->at[13] * b->at[11];

  c->at[10] = a->at[2]  * b->at[8]
            + a->at[6]  * b->at[9]
            + a->at[10] * b->at[10]
            + a->at[14] * b->at[11];

  c->at[11] = a->at[3]  * b->at[8]
            + a->at[7]  * b->at[9]
            + a->at[11] * b->at[10]
            + a->at[15] * b->at[11];

  c->at[12] = a->at[0]  * b->at[12]
            + a->at[4]  * b->at[13]
            + a->at[8]  * b->at[14]
            + a->at[12] * b->at[15];

  c->at[13] = a->at[1]  * b->at[12]
            + a->at[5]  * b->at[13]
            + a->at[9]  * b->at[14]
            + a->at[13] * b->at[15];

  c->at[14] = a->at[2]  * b->at[12]
            + a->at[6]  * b->at[13]
            + a->at[10] * b->at[14]
            + a->at[14] * b->at[15];

  c->at[15] = a->at[3]  * b->at[12]
            + a->at[7]  * b->at[13]
            + a->at[11] * b->at[14]
            + a->at[15] * b->at[15];

  return c;
}

SuperMatrix* SuperMatrix_orthographic( SuperMatrix* m, int width, int height, double depth )
{
  SuperMatrix_identity( m );

  m->at[0]  =  2.0 /  (width);
  m->at[5]  = -2.0 / (height);
  m->at[10] = -2.0 / depth;
  m->at[12] = -1.0;
  m->at[13] =  1.0;
  m->at[14] = -1.0;

  return m;
}

SuperReal32* SuperMatrix_to_real32( SuperMatrix* m, SuperReal32* dest )
{
  dest[0]  = (SuperReal32) m->at[0];
  dest[1]  = (SuperReal32) m->at[1];
  dest[2]  = (SuperReal32) m->at[2];
  dest[3]  = (SuperReal32) m->at[3];
  dest[4]  = (SuperReal32) m->at[4];
  dest[5]  = (SuperReal32) m->at[5];
  dest[6]  = (SuperReal32) m->at[6];
  dest[7]  = (SuperReal32) m->at[7];
  dest[8]  = (SuperReal32) m->at[8];
  dest[9]  = (SuperReal32) m->at[9];
  dest[10] = (SuperReal32) m->at[10];
  dest[11] = (SuperReal32) m->at[11];
  dest[12] = (SuperReal32) m->at[12];
  dest[13] = (SuperReal32) m->at[13];
  dest[14] = (SuperReal32) m->at[14];
  dest[15] = (SuperReal32) m->at[15];
  return dest;
}

SuperMatrix* SuperMatrix_zeros( SuperMatrix* m )
{
  memset( m->at, 0, sizeof(void*) * 16 );
  return m;
}


//=============================================================================
//  SuperGraphics
//=============================================================================

//-----------------------------------------------------------------------------
//  GLOBALS
//-----------------------------------------------------------------------------
SuperGraphicsDrawBuffer  SuperGraphics;
SuperInteger             SuperGraphics_alpha_multiplication_lookup[65536];


//-----------------------------------------------------------------------------
//  LOCAL FUNCTIONS
//-----------------------------------------------------------------------------
static void SuperGraphics_ensure_additional_vertex_capacity( int count )
{
  if (SuperGraphics.vertex_count + count > SUPER_MAX_BUFFERED_VERTICES)
  {
    SuperGraphics_flush();
  }
}

#define SuperGraphics_get_vertex() &SuperGraphics.vertices[SuperGraphics.vertex_count++]

static void SuperGraphics_set_draw_mode( int mode )
{
  if (SuperGraphics.draw_mode != mode)
  {
    SuperGraphics_flush();
    SuperGraphics.draw_mode = mode;
  }
}

//-----------------------------------------------------------------------------
//  FUNCTIONS
//-----------------------------------------------------------------------------
void SuperGraphicsDrawBuffer_init()
{
  int a, rgb;

  memset( &SuperGraphics, 0, sizeof(SuperGraphicsDrawBuffer) );

  SuperMatrix_identity( &SuperGraphics.projection_transform );

  for (a=0; a<=255; ++a)
  {
    for (rgb=0; rgb<=255; ++rgb)
    {
      SuperGraphics.alpha_multiplication_lookup[ (a<<8) | rgb ] = (SuperByte) ((rgb * a) / 255);
    }
  }
}

/*
void SuperGraphics_configure();
void SuperGraphics_retire();

void SuperGraphics_clip( int x, int y, int w, int h );
void SuperGraphics_configure_font_system();
int  SuperGraphics_create_texture_from_png_file( const char* filepath, int flags );
void SuperGraphics_destroy_texture( int texture_id );
*/

int SuperGraphics_create_texture_from_file( const char* filepath, int flags )
{
  filepath = Super_locate_asset( "", filepath );
  if ( !filepath ) return 0;

  int   width, height, bits_per_pixel;
  void* data;
  int   result;

  data = SuperGraphics_load_pixel_data_from_png_file( filepath, &width, &height, &bits_per_pixel );

  if ( !data ) return 0;

  if (bits_per_pixel != 32)
  {
    SUPER_FREE( data );
    return 0;
  }

  result = SuperGraphics_create_texture( width, height, data, flags );

  SUPER_FREE( data );

  return result;
}

void SuperGraphics_draw_line( double x1, double y1, double x2, double y2, int color )
{
  SuperGraphics_set_draw_mode( SUPER_DRAW_LINES );
  SuperGraphics_ensure_additional_vertex_capacity( 2 );

  {
    SuperGraphicsVertex* v1 = SuperGraphics_get_vertex();
    SuperGraphicsVertex* v2 = SuperGraphics_get_vertex();

    v1->x = (SuperReal32) x1;
    v1->y = (SuperReal32) y1;
    v1->z = 0.0f;
    v1->color = (SuperInteger) color;

    v2->x = (SuperReal32) x2;
    v2->y = (SuperReal32) y2;
    v2->z = 0.0f;
    v2->color = (SuperInteger) color;
  }
}

void SuperGraphics_fill_colored_triangle( double x1, double y1, int color1, double x2, double y2, int color2, double x3, double y3, int color3 )
{
  SuperGraphics_set_draw_mode( SUPER_DRAW_SOLID_TRIANGLES );
  SuperGraphics_ensure_additional_vertex_capacity( 3 );

  {
    SuperGraphicsVertex* a = SuperGraphics_get_vertex();
    SuperGraphicsVertex* b = SuperGraphics_get_vertex();
    SuperGraphicsVertex* c = SuperGraphics_get_vertex();

    a->x = (SuperReal32) x1;
    a->y = (SuperReal32) y1;
    a->z = 0.0f;
    a->color = (SuperInteger) color1;

    b->x = (SuperReal32) x2;
    b->y = (SuperReal32) y2;
    b->z = 0.0f;
    b->color = (SuperInteger) color2;

    c->x = (SuperReal32) x3;
    c->y = (SuperReal32) y3;
    c->z = 0.0f;
    c->color = (SuperInteger) color3;
  }
}

void SuperGraphics_fill_textured_box( double x1, double y1, double w, double h, 
                                      double u1, double v1, double u2, double v2, int color, 
                                      int texture_id )
{
  double x2 = x1 + w;
  double y2 = y1 + h;

  SuperGraphics_fill_textured_triangle( x1,y1,u1,v1,color,
                                        x2,y1,u2,v1,color,
                                        x2,y2,u2,v2,color,
                                        texture_id );

  SuperGraphics_fill_textured_triangle( x1,y1,u1,v1,color,
                                        x2,y2,u2,v2,color,
                                        x1,y2,u1,v2,color,
                                        texture_id );
}

void SuperGraphics_fill_textured_triangle( double x1, double y1, double u1, double v1, int color1,
                                           double x2, double y2, double u2, double v2, int color2,
                                           double x3, double y3, double u3, double v3, int color3,
                                           int texture_id )
{
  SuperGraphics_set_draw_mode( SUPER_DRAW_TEXTURED_TRIANGLES );
  SuperGraphics_ensure_additional_vertex_capacity( 3 );

  if (texture_id != SuperGraphics.texture_id)
  {
    SuperGraphics_flush();
    SuperGraphics.texture_id = texture_id;
  }

  {
    SuperGraphicsVertex* a = SuperGraphics_get_vertex();
    SuperGraphicsVertex* b = SuperGraphics_get_vertex();
    SuperGraphicsVertex* c = SuperGraphics_get_vertex();

    a->x = (SuperReal32) x1;
    a->y = (SuperReal32) y1;
    a->z = 0.0f;
    a->u = (SuperReal32) u1;
    a->v = (SuperReal32) v1;
    a->color = (SuperInteger) color1;

    b->x = (SuperReal32) x2;
    b->y = (SuperReal32) y2;
    b->z = 0.0f;
    b->u = (SuperReal32) u2;
    b->v = (SuperReal32) v2;
    b->color = (SuperInteger) color2;

    c->x = (SuperReal32) x3;
    c->y = (SuperReal32) y3;
    c->z = 0.0f;
    c->u = (SuperReal32) u3;
    c->v = (SuperReal32) v3;
    c->color = (SuperInteger) color3;
  }
}

void SuperGraphics_premultiply_alpha( SuperInteger* data, int count )
{
  --data;
  while (--count >= 0)
  {
    int color = *(++data);
    int a = (color >> 16) & 0xff00;
    int r = SuperGraphics.alpha_multiplication_lookup[ a | ((color >> 16) & 255) ];
    int g = SuperGraphics.alpha_multiplication_lookup[ a | ((color >> 8) & 255) ];
    int b = SuperGraphics.alpha_multiplication_lookup[ a | (color & 255) ];
    *data = (SuperInteger) ((a << 16) | (r << 16) | (g << 8) | b);
  }
}

/*
int  SuperGraphics_get_texture_info( int texture_id, int* image_width_ptr, int* image_height_ptr, 
                                     int* texture_width_ptr, int* texture_height_ptr, int* bits_per_pixel_ptr );
int  SuperGraphics_load_font( const char* filepath );
SuperInteger* SuperGraphics_load_pixel_data_from_png_file( const char* filepath, int* width_ptr, int* height_ptr );

SuperByte* SuperGraphics_render_font_character( int font_id, int unicode, int* pixel_width, int* pixel_height, 
                                                int* offset_x, int* offset_y, int* advance_x, int* advance_y, int* pitch );

void SuperGraphics_retire_font_system();
void SuperGraphics_set_font_pixel_size( int font_id, int pixel_width, int pixel_height );
*/

void SuperGraphics_set_projection_transform( SuperMatrix* m )
{
  SuperMatrix_copy( m, &SuperGraphics.projection_transform );
}

void SuperGraphics_set_render_mode( int render_mode )
{
  if (SuperGraphics.render_mode != render_mode)
  {
    SuperGraphics_flush();
    SuperGraphics.render_mode = render_mode;
  }
}

void SuperGraphics_swap_red_and_blue( SuperInteger* data, int count )
{
  --data;
  while (--count >= 0)
  {
    SuperInteger c = *(++data);
    *(data) = (c & 0xff00ff00) | ((c >> 16) & 0xff) | ((c << 16) & 0xff0000);
  }
}

