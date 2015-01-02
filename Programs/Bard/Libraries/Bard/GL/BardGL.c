//
//  BardGLCore.c
//  BardRendererTest
//
//  Created by Michael Dwan on 8/9/13.
//  Copyright (c) 2013 AdColony. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "BardGL.h"
//#include "BardObjCInterface.h"
//#include "BardObjCInterface.h"

// PROTOTYPES
int  BardObjC_load_texture( char* fileName, BardGLTexture* dest );


// GLOBALS
BardGLContext    BardGL_context = {0};
BardResourceBank BardGL_texture_bank = {0};
BardGLTexture* BardGL_active_texture = NULL;

BardGL_render_ptr BardGL_internal_render = NULL;


//Built-in shaders
BardGLShader BardGL_opaque_shader =
{
  BARD_OPAQUE_SHADER,
  FALSE,
  // vertex_src
  "uniform mat4 model_view; \n"
  "uniform mat4 projection; \n"
  "attribute vec4 pos; \n"
  "attribute vec4 src_color; \n"
  "varying vec4 dest_color; \n"
  "void main(void) { \n"
  "    dest_color = src_color/255.0; \n"
  "    gl_Position = projection*pos; \n"
  // "    gl_Position = model_view*projection*pos; \n"
  "} \n",

  // fragment_src
  "varying lowp vec4 dest_color; \n"
  "void main(void) { \n"
  "    gl_FragColor = dest_color; \n"
  "} \n"
};

BardGLShader BardGL_texture_shader =
{
  BARD_TEXTURE_SHADER,
  FALSE,
  // vertex_src
  "uniform mat4 model_view; \n"
  "uniform mat4 projection; \n"
  "attribute vec4 pos; \n"
  "attribute vec4 src_color; \n"
  "varying vec4 dest_color; \n"
  "attribute vec2 uv_in; \n"
  "varying vec2 uv_out; \n"
  "void main(void) { \n"
  "    dest_color = src_color/255.0; \n"
  "    gl_Position = projection*pos; \n"
  // "    gl_Position = model_view*projection*pos; \n"
  "    uv_out = uv_in; \n"
  "} \n",

  // fragment_src
  "varying lowp vec4 dest_color; \n"
  "varying lowp vec2 uv_out; \n"
  "uniform sampler2D texture; \n"
  "void main(void) { \n"
  "    gl_FragColor = dest_color*texture2D(texture, uv_out); \n"
  "} \n"
};


BardGLShader BardGL_fixed_color_texture_shader =
{
  BARD_FIXEDCOLORTEXTURE_SHADER,
  FALSE,
  // vertex_src
  "uniform mat4 model_view; \n"
  "uniform mat4 projection; \n"
  "attribute vec4 pos; \n"
  "attribute vec4 src_color; \n"
  "varying vec4 dest_color; \n"
  "attribute vec2 uv_in; \n"
  "varying vec2 uv_out; \n"
  "void main(void) { \n"
  "    dest_color = src_color/255.0; \n"
  "    gl_Position = model_view*projection*pos; \n"
  "    uv_out = uv_in; \n"
  "} \n",

  // fragment_src
  "varying lowp vec4 dest_color; \n"
  "varying lowp vec2 uv_out; \n"
  "uniform sampler2D texture; \n"
  "void main(void) { \n"
  "    gl_FragColor = dest_color*texture2D(texture, uv_out); \n"
  "} \n"
};



// Adds the specified (x,y) coordinates, color, and (u,v) coordinates to the
// appropriate drawing buffers.  The current combined MVP transform will be
// applied to the vertex during rendering.
short BardGL_add_vertex_2d( double x, double y, unsigned int aaRRggBB, double u, double v, BOOL index )
{
  BardGLVertex2* vert = &BardGL_context.vertex_list[BardGL_context.vertex_count++];

  vert->position[0]  = x;
  vert->position[1]  = y;
  vert->uv_coords[0] = u;
  vert->uv_coords[1] = v;
  vert->argb = aaRRggBB;

  if (index)
  {
    BardGL_add_vertex_index( BardGL_context.vertex_count-1 );
    return BardGL_context.index_list[BardGL_context.index_count-1];
  }

  return BardGL_context.vertex_count-1;
}

void BardGL_add_vertex_index( short i )
{
  BardGL_context.index_list[BardGL_context.index_count++] = i;
}

// Called when a new frame is about to be drawn.  All other calls except
// unload() but including BardGL_clear() will take place between this call and
// end_draw().
void BardGL_begin_draw( int display_width, int display_height )
{
  BardGL_context.display_width = display_width;
  BardGL_context.display_height = display_height;

  BardGL_load_ortho_projection( 0, 0, display_width, display_height, -5, 5 );

  glBindBuffer( GL_ARRAY_BUFFER, BardGL_context.vertex_buffer );
  glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, BardGL_context.index_buffer );
  glDisable( GL_STENCIL_TEST );
  glDisable( GL_DEPTH_TEST );

  BardGL_enable_alpha_blending( TRUE );
  BardGL_use_blend_mode( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
}

// Calls render() and then clears the framebuffer to be the given color.  If
// aaRRggBB is 0 then the screen is not cleared.  Also clears the view transform
// stack.
void BardGL_clear( unsigned int aaRRggBB )
{
  GLubyte a,r,g,b;
  BardGL_render();
  BardGL_clear_view_transform();
  if (aaRRggBB > 0)
  {
    SEPARATE_ARGB32(aaRRggBB,a,r,g,b);
    glClearColor(r/255.0, g/255.0, b/255.0, 1.0);
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
  }
  else
  {
    glClear( GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
  }
}

// [render]
// Clears the view transform stack and flags it as having changed.
void BardGL_clear_view_transform()
{
  BardGL_context.view_matrix_stack.sp = 0;
  BardGL_context.view_matrix_stack.changed = TRUE;
  BardGL_render();
}

// Convenience call that sets line rendering mode and adds two vertices to the
// drawing buffer.
void BardGL_draw_line_2d( double x1, double y1, double x2, double y2, unsigned int aaRRggBB )
{
  if (BardGL_context.draw_mode != BARD_DRAW_MODE_LINES)
  {
    BardGL_render();
    BardGL_context.draw_mode = BARD_DRAW_MODE_LINES;
  }

  if(BardGL_context.vertex_count+2 > BARD_VERTEX_LIST_SIZE) BardGL_render();


  BardGL_add_vertex_2d( x1, y1, aaRRggBB, 0, 0, TRUE );
  BardGL_add_vertex_2d( x2, y2, aaRRggBB, 0, 0, TRUE );
}

// Convenience call that sets triangle rendering mode and a single triangle (3
// vertices) to the drawing buffer, each vertex with the given color.
void BardGL_draw_triangle_2d( double x1, double y1, double x2, double y2, double x3, double y3,
                       double u1, double v1, double u2, double v2, double u3, double v3,
                       unsigned int aaRRggBB )
{
  if (BardGL_context.draw_mode != BARD_DRAW_MODE_TRIANGLES)
  {
    BardGL_render();
    BardGL_context.draw_mode = BARD_DRAW_MODE_TRIANGLES;
  }

  if(BardGL_context.vertex_count+3 > BARD_VERTEX_LIST_SIZE) BardGL_render();

  BardGL_add_vertex_2d( x1, y1, aaRRggBB, u1, v1, TRUE );
  BardGL_add_vertex_2d( x2, y2, aaRRggBB, u2, v2, TRUE );
  BardGL_add_vertex_2d( x3, y3, aaRRggBB, u3, v3, TRUE );
}

// Convenience call that sets triangle rendering mode and adds two triangles (6
// vertices) to the drawing buffer.  (x1,y1) is the top left and (x2,y2) is the
// bottom right.  Each vertex shares the same color.  Boxes are implicitly 2D.
void BardGL_draw_box( double x1, double y1, double x2, double y2,
               double u1, double v1, double u2, double v2,
               unsigned int aaRRggBB )
{
  short index0, index1, index2, index3;

  if (BardGL_context.draw_mode != BARD_DRAW_MODE_TRIANGLES)
  {
    BardGL_render();
    BardGL_context.draw_mode = BARD_DRAW_MODE_TRIANGLES;
  }

  if(BardGL_context.vertex_count+4 > BARD_VERTEX_LIST_SIZE) BardGL_render();

  index0 = BardGL_add_vertex_2d( x1, y1, aaRRggBB, u1, v1, FALSE );
  index1 = BardGL_add_vertex_2d( x1, y2, aaRRggBB, u1, v2, FALSE );
  index2 = BardGL_add_vertex_2d( x2, y2, aaRRggBB, u2, v2, FALSE );
  index3 = BardGL_add_vertex_2d( x2, y1, aaRRggBB, u2, v1, FALSE );

  BardGL_add_vertex_index( index0 );
  BardGL_add_vertex_index( index1 );
  BardGL_add_vertex_index( index2 );
  BardGL_add_vertex_index( index2 );
  BardGL_add_vertex_index( index3 );
  BardGL_add_vertex_index( index0 );
}

// Convenience call that sets triangle rendering mode and adds two triangles (6
// vertices) to the drawing buffer representing a box with its center at (xc,yc)
// and size (w,h) rotated by 'radians'.  Boxes are implicitly 2D.
// (x1,y1) = (-w,-h)
// (x2,y2) = ( w,-h)
// (x3,y3) = ( w, h)
// (x4,y4) = (-w, h)

// [Apply 2D rotation formula to each (x,y) and add vertices.]
void BardGL_draw_box_rotated( double xc, double yc, double w, double h,
                       double u1, double v1, double u2, double v2,
                       unsigned int aaRRggBB, double radians)
{
  short index0, index1, index2, index3;
  double x1,y1, x2,y2, x3,y3, x4,y4;
  double cos_theta, sin_theta;
  double w_over_2_cos, h_over_2_cos, w_over_2_sin, h_over_2_sin;

  if (BardGL_context.draw_mode != BARD_DRAW_MODE_TRIANGLES)
  {
    BardGL_render();
    BardGL_context.draw_mode = BARD_DRAW_MODE_TRIANGLES;
  }

  if(BardGL_context.vertex_count+4 > BARD_VERTEX_LIST_SIZE) BardGL_render();

  cos_theta = cos(radians);
  sin_theta = sin(radians);

  w_over_2_cos = (w/2.0)*cos_theta;
  h_over_2_cos = (h/2.0)*cos_theta;
  w_over_2_sin = (w/2.0)*sin_theta;
  h_over_2_sin = (h/2.0)*sin_theta;

  // x = -w_over_2, y = -h_over_2
  x2 = -w_over_2_cos - (-h_over_2_sin);
  y4 = -w_over_2_sin + (-h_over_2_cos);
  // x = +w_over_2, y = -h_over_2
  x3 = w_over_2_cos - (-h_over_2_sin);
  y1 = w_over_2_sin + (-h_over_2_cos);
  // x = +w_over_2, y = +h_over_2
  x4 = w_over_2_cos - h_over_2_sin;
  y2 = w_over_2_sin + h_over_2_cos;
  // x = -w_over_2, y = h_over_2
  x1 = -w_over_2_cos - h_over_2_sin;
  y3 = -w_over_2_sin + h_over_2_cos;

  index0 = BardGL_add_vertex_2d( xc + x1, yc + y1, aaRRggBB, u1, v1, FALSE );
  index1 = BardGL_add_vertex_2d( xc + x2, yc + y2, aaRRggBB, u1, v2, FALSE );
  index2 = BardGL_add_vertex_2d( xc + x3, yc + y3, aaRRggBB, u2, v2, FALSE );
  index3 = BardGL_add_vertex_2d( xc + x4, yc + y4, aaRRggBB, u2, v1, FALSE );

  BardGL_add_vertex_index( index0 );
  BardGL_add_vertex_index( index1 );
  BardGL_add_vertex_index( index2 );
  BardGL_add_vertex_index( index2 );
  BardGL_add_vertex_index( index3 );
  BardGL_add_vertex_index( index0 );
}


// [shader flag]
// [render on change]
// Turns alpha blending on or off.
void BardGL_enable_alpha_blending( BOOL setting )
{
  if (setting == BardGL_context.blending_enabled) return;

  BardGL_render();
  BardGL_context.blending_enabled = setting;
  if (setting) glEnable( GL_BLEND );
  else glDisable( GL_BLEND );
}

// [shader flag]
// [render on change]
// Enables fixed-color multiplication that is potentially combined with
// texturing and/or per-vertex color.
void BardGL_enable_fixed_color( unsigned int aaRRggBB )
{
  if (BardGL_context.curr_shader->id == BARD_FIXEDCOLORTEXTURE_SHADER &&
      aaRRggBB == BardGL_context.constant_color )
  {
    return;
  }

  BardGL_render();
  // BardGL_use_builtin_shader(&BardGL_fixed_color_texture_shader);
  BardGL_context.draw_mode = BARD_DRAW_MODE_TRIANGLES;
  BardGL_context.constant_color = aaRRggBB;
}


// [shader flag]
// [render on change]
// Enables or disables textured triangles.  The texture used is specified with
// use_texture(int).
void BardGL_enable_texture( BOOL setting )
{
  if (setting != BardGL_context.texturing_enabled) BardGL_render();
  BardGL_context.texturing_enabled = setting;
}

// Calls render(), glFinish(), and performs any other necessary state updates.
void BardGL_end_draw()
{
  BardGL_render();
}

// Returns the rendering display area in pixels for current orientation.
// Retina screens should return TRUE pixel resolution; e.g. iPad 2 in
// fullscreen landscape returns 1024x768 while iPad 3 returns 2048x1536.
int BardGL_get_display_width()
{
  return BardGL_context.display_width;
}

int BardGL_get_display_height()
{
  return BardGL_context.display_height;
}

BardGLTexture* BardGL_get_texture( int resource_id )
{
  return BardResourceBank_get( &BardGL_texture_bank, resource_id );
}

// Returns the original image width and height of a given texture before padding
// was added (if any) to make it a power of 2 in size.
int BardGL_get_image_width( int texture_id )
{
  BardGLTexture* tex = BardResourceBank_get( &BardGL_texture_bank, texture_id );
  return tex ? tex->image_width : 0;
}

int BardGL_get_image_height( int texture_id )
{
  BardGLTexture* tex = BardResourceBank_get( &BardGL_texture_bank, texture_id );
  return tex ? tex->image_height : 0;
}

// Returns the power-of-2 texture width and height of a given texture.
int BardGL_get_texture_width( int texture_id )
{
  BardGLTexture* tex = BardResourceBank_get( &BardGL_texture_bank, texture_id );
  return tex ? tex->texture_width : 0;
}

int BardGL_get_texture_height( int texture_id )
{
  BardGLTexture* tex = BardResourceBank_get( &BardGL_texture_bank, texture_id );
  return tex ? tex->texture_width : 0;
}

// Returns the pixel density in pixels per inch - e.g. 132 for iPad 2.
int BardGL_get_pixels_per_inch()
{

  return 0;
}

// Returns TRUE if this device is a tablet.
BOOL BardGL_is_tablet()
{
  return FALSE;
}

// Loads image at pre-validated filepath, returns integer ID of texture.
// Textures are only unloaded by a call to reset().  The original image is
// padded with extra space on the right and bottom to make the texture a power
// of 2.  The original image_width and image_height should be kept in addition
// to the power-of-2 texture_width and texture_height.
int BardGL_load_texture( char* filepath )
{
  int resource_id;
  BardGLTexture* texture = (BardGLTexture*) BARD_ALLOCATE( sizeof(BardGLTexture) );
  if ( !BardObjC_load_texture( filepath, texture ) )
  {
    BARD_FREE( texture );
    return 0;
  }

  resource_id = BardResourceBank_add( &BardGL_texture_bank, texture );
  texture->resource_id = resource_id;

  return resource_id;
}

// Adds the given matrix to the view matrix stack and flags the view matrix
// stack as having changed.
void BardGL_push_view_matrix( GLfloat m[16] )
{
  BardGL_context.view_matrix_stack.changed = TRUE;
  GLfloat* mat = BardGL_context.view_matrix_stack.el[BardGL_context.view_matrix_stack.sp++];
  mat[0]  = m[0];
  mat[1]  = m[1];
  mat[2]  = m[2];
  mat[3]  = m[3];
  mat[4]  = m[4];
  mat[5]  = m[5];
  mat[6]  = m[6];
  mat[7]  = m[7];
  mat[8]  = m[8];
  mat[9]  = m[9];
  mat[10] = m[10];
  mat[11] = m[11];
  mat[12] = m[12];
  mat[13] = m[13];
  mat[14] = m[14];
  mat[15] = m[15];
}

// Calculates a view matrix from the supplied transformation parameters and
// pushes it onto the stack, flagging the stack as having changed.
// See [3. Matrix] below for algorithm.
void BardGL_push_view_transform( double width,  double height,
                               double handle_x, double handle_y,
                               double radians,
                               double scale_x, double scale_y,
                               BOOL hflip, BOOL vflip )
{
  int hflip_sign = 1;
  int vflip_sign = 1;

  if (hflip || vflip)
  {
    handle_x -= width/2.0;
    handle_y -= height/2.0;
    if (hflip) hflip_sign = -1;
    if (vflip) vflip_sign = -1;
  }

  //double cost = cos(radians);
  //double sint = sin(radians);

  // double r1c1 = cost * scale_x * hflip_sign;
  // double r1c2 = -sint * scale_y * vflip_sign;
  // double r1c4 = pos.x - scale_x*handle_x*cost + sint*scale_y*handle_y;

  // double r2c1 = sint * scale_x * hflip_sign;
  // double r2c2 = cost * scale_y * vflip_sign;
  // double r2c4 = pos.y - scale_x*handle_x*sint - cost*scale_y*handle_y;

  // push_view_matrix( r1c1, r1c2,    0, r1c4,
  //                   r2c1, r2c2,    0, r2c4,
  //                      0,    0,    1,    0,
  //                      0,    0,    0,    1 );
  // if (hflip || vflip)
  // {
  //   // translate by -size/2
  //   push_view_matrix( 1, 0, 0,  -width/2,
  //                     0, 1, 0, -height/2,
  //                     0, 0, 1,         0,
  //                     0, 0, 0,         1 );
  // }
}

// Decrements the view matrix stack count and flags the view matrix stack as
// having changed.
void BardGL_pop_view_matrix()
{
  BardGL_context.view_matrix_stack.sp--;
  BardGL_context.view_matrix_stack.changed = TRUE;
}

// If there are any queued vertices:

// Multiplies out the view matrix stack (if changed) and then multiplies the
// projection transform by the combined view matrix (if either has
// changed).  A future version of the renderer will include a Model transform
// stack in this process as well.

// MVP = P * (( (V[0]*V[1]) * V[2] ) * ...)

// If the View Stack is empty then just the projection matrix is used as the
// combined MVP matrix.

// Sets up GL environment including various glEnable calls (texture, blending)
// as appropriate.

// Performs a glDrawElements on the buffered vertices.
void BardGL_render()
{
  GLuint ret;
  GLenum draw_type = GL_TRIANGLES;
  if (BardGL_context.vertex_count == 0) return;
  // printf("VERTEX COUNT: %d\n", BardGL_context.vertex_count);
  // BardGL_debug_print_vertices(&BardGL_context);
  // printf("INDEX COUNT: %d\n", BardGL_context.index_count);
  // BardGL_debug_print_indices(&BardGL_context);

  glViewport( 0, 0, BardGL_get_display_width(), BardGL_get_display_height() );
  switch(BardGL_context.draw_mode)
  {
    case BARD_DRAW_MODE_TRIANGLES:
      if (BardGL_context.texturing_enabled)
      {
        BardGL_use_builtin_shader(&BardGL_texture_shader);
        break;
      }
    case BARD_DRAW_MODE_LINES:
    case BARD_DRAW_MODE_POINTS:
      BardGL_use_builtin_shader(&BardGL_opaque_shader);
      break;
  }

  BardGL_internal_render();

  glBufferSubData( GL_ARRAY_BUFFER, 0,
                  sizeof(BardGLVertex2)*BardGL_context.vertex_count,
                  BardGL_context.vertex_list );
  ret = glGetError();

  glBufferSubData( GL_ELEMENT_ARRAY_BUFFER, 0,
                  sizeof(short)*BardGL_context.index_count,
                  BardGL_context.index_list );
  ret = glGetError();


  if(BardGL_context.draw_mode == BARD_DRAW_MODE_POINTS) draw_type = GL_POINTS;
  if(BardGL_context.draw_mode == BARD_DRAW_MODE_LINES) draw_type = GL_LINES;
  glDrawElements( draw_type, BardGL_context.index_count,
                  GL_UNSIGNED_SHORT, 0 );
  ret = glGetError();

  BardGL_context.vertex_count = 0;
  BardGL_context.index_count  = 0;
}

// [render on change]
// Sets point rendering mode (one vertex per point).  BardGL_add_vertex_2d() is used to
// add point vertices.
void BardGL_set_point_rendering_mode()
{
  if (BardGL_context.draw_mode != BARD_DRAW_MODE_POINTS)
  {
    BardGL_render();
    BardGL_context.draw_mode = BARD_DRAW_MODE_POINTS;
  }
}

// [render on change]
// Sets line rendering mode (two vertices per line).  Each line is considered
// independent from the others.  BardGL_add_vertex_2d() is used to add line vertices.
void BardGL_set_line_rendering_mode()
{
  if (BardGL_context.draw_mode != BARD_DRAW_MODE_LINES)
  {
    BardGL_render();
    BardGL_context.draw_mode = BARD_DRAW_MODE_LINES;
  }
}

// [render on change]
// Sets the projection transform matrix.
// See also: [clear/push/pop]ViewMatrix
void BardGL_set_projection_matrix( GLfloat m[16] )
{
  GLfloat* mat = BardGL_context.projection_matrix;
  mat[0]  = m[0];
  mat[1]  = m[1];
  mat[2]  = m[2];
  mat[3]  = m[3];
  mat[4]  = m[4];
  mat[5]  = m[5];
  mat[6]  = m[6];
  mat[7]  = m[7];
  mat[8]  = m[8];
  mat[9]  = m[9];
  mat[10] = m[10];
  mat[11] = m[11];
  mat[12] = m[12];
  mat[13] = m[13];
  mat[14] = m[14];
  mat[15] = m[15];
}

// [render on change]
// Sets triangle rendering mode (three vertices per triangle).  Each triangle
// is considered independent from the others.  BardGL_add_vertex_2d() is used to add
// triangle vertices.
void BardGL_set_triangle_rendering_mode()
{
  if (BardGL_context.draw_mode != BARD_DRAW_MODE_TRIANGLES)
  {
    BardGL_render();
    BardGL_context.draw_mode = BARD_DRAW_MODE_TRIANGLES;
  }
}

// [render on change]
// Sets textured triangle rendering mode (three vertices per triangle).  Each triangle
// is considered independent from the others.  BardGL_add_vertex_2d() is used to add
// triangle vertices.
//void BardGL_set_textured_triangle_rendering_mode()
//{
//  if (BardGL_context.draw_mode != BARD_DRAW_MODE_TEXTURED_TRIANGLES)
//  {
//    BardGL_render();
//    BardGL_context.draw_mode = BARD_DRAW_MODE_TEXTURED_TRIANGLES;
//  }
//}

// Sets each dimension to either wrap (if TRUE) or clamp (if FALSE).
// glTexParameter( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S/T,
//                 GL_REPEAT/GL_CLAMP_TO_EDGE );
void BardGL_set_texture_wrap( BOOL w, BOOL h )
{
  GLenum s_wrap = w ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  GLenum t_wrap = h ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, s_wrap );
  glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, t_wrap );
}

// Unloads/frees all resources - textures, etc.
void BardGL_unload()
{
  int resource_id = BardResourceBank_find_first( &BardGL_texture_bank );
  while (resource_id)
  {
    BardGLTexture* texture = BardResourceBank_remove( &BardGL_texture_bank, resource_id );
    glDeleteTextures( 1, &texture->id );
    BARD_FREE( texture );

    resource_id = BardResourceBank_find_first( &BardGL_texture_bank );
  }

  BardResourceBank_clear( &BardGL_texture_bank );
}

// [render on change]
// Calls glBlendFunc(src_blend,dest_blend).  The parameters are the same
// enumerated values that OpenGL and glBlendFunc uses.  Note: we won't use
// glBlendFuncSeparate as it is not supported on Samsung GS2.
void BardGL_use_blend_mode( GLenum src_blend, GLenum dest_blend )
{
  if ( BardGL_context.src_blend == src_blend &&
       BardGL_context.dest_blend == dest_blend )
  {
    return;
  }

  BardGL_render();

  BardGL_context.src_blend = src_blend;
  BardGL_context.dest_blend = dest_blend;
  glBlendFunc( src_blend, dest_blend );
}

// [shader flag]
// [render on change]
// Use the given texture's alpha for subsequent drawing instead of the alpha
// in the texture specified by setTexture().  Sending a 'texture_id' of -1
// will disable the alpha source flag.
void BardGL_use_alpha_source( int texture_id )
{

}

// [render on change]
// Sets the linear filter for texturing which smooths a texture as it is scaled.
// glTexParameter( GL_TEXTURE_2D, GL_TEXTURE_MIN/MAG_FILTER, GL_LINEAR );
void BardGL_use_linear_filter()
{
  if(BardGL_context.texture_filter == GL_LINEAR) return;

  BardGL_context.texture_filter = GL_LINEAR;
  glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
  glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
}

// [render on change]
// Sets the nearest neighbor filter for texturing which keeps each texel an
// whole number of pixels in size.
// glTexParameter( GL_TEXTURE_2D, GL_TEXTURE_MIN/MAG_FILTER, GL_NEAREST );
void BardGL_use_nearest_neighbor_filter()
{
  if(BardGL_context.texture_filter == GL_NEAREST) return;

  BardGL_context.texture_filter = GL_NEAREST;
  glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
  glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
}

// [render on change]
// Uses the given texture for subsequent drawing commands at the zero-based
// stage specified.  "-1" will be sent to disable a given stage.
void BardGL_use_texture( int texture_id, int stage )
{
  if (BardGL_active_texture && texture_id == BardGL_active_texture->resource_id) return;
  BardGL_render();

  BardGL_enable_texture( texture_id != 0 );

  BardGL_active_texture = (BardGLTexture*) BardResourceBank_get( &BardGL_texture_bank, texture_id );
  // BardGL_set_triangle_rendering_mode();
}



// OpenGL setup and Shader functions
void BardGL_init()
{
  BardGL_shader_build( &BardGL_texture_shader );
  BardGL_shader_build( &BardGL_opaque_shader );
  BardGL_init_gl_vertex_and_index_buffers();
}

void BardGL_init_gl_renderbuffer()
{
  glGenRenderbuffers( 1, &BardGL_context.color_renderbuffer );
  glBindRenderbuffer( GL_RENDERBUFFER, BardGL_context.color_renderbuffer );
}

void BardGL_init_gl_framebuffer()
{
  glGenFramebuffers( 1, &BardGL_context.framebuffer );
  glBindFramebuffer( GL_FRAMEBUFFER, BardGL_context.framebuffer );
  glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER,
                             BardGL_context.color_renderbuffer );
}

void BardGL_init_gl_vertex_and_index_buffers()
{
    glGenBuffers( 1, &BardGL_context.vertex_buffer );
    glBindBuffer( GL_ARRAY_BUFFER, BardGL_context.vertex_buffer );
    glBufferData( GL_ARRAY_BUFFER, BARD_VERTEX_LIST_SIZE*sizeof(BardGLVertex2),
                  NULL, GL_STATIC_DRAW );

    glGenBuffers( 1, &BardGL_context.index_buffer );
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, BardGL_context.index_buffer );
    glBufferData( GL_ELEMENT_ARRAY_BUFFER, BARD_INDEX_LIST_SIZE*sizeof(short),
                  NULL, GL_STATIC_DRAW );
}

GLuint BardGL_shader_compile( BardGLShader* shader, GLenum shader_type )
{
  GLuint shader_handle = glCreateShader(shader_type);
  char* src = (shader_type == GL_FRAGMENT_SHADER) ? shader->fragment_src : shader->vertex_src;

  int src_length = (int) strlen(src);

  GLint compilation_successful;
  GLchar messages[256];

  glShaderSource( shader_handle, 1, (const GLchar* const*) &src, &src_length );
  glCompileShader( shader_handle );

  glGetShaderiv( shader_handle, GL_COMPILE_STATUS, &compilation_successful );
  if (compilation_successful == GL_FALSE) {
    glGetShaderInfoLog( shader_handle, sizeof(messages), 0, &messages[0] );
    printf(">>>>>>>Shader Compilation ERROR: %s", (char*) messages);
    // TODO: wrapper to print to Xcode console with NSLog
    exit( 1 );
  }

  return shader_handle;
}

int BardGL_shader_build( BardGLShader* shader )
{
  shader->vertex_shader   = BardGL_shader_compile( shader, GL_VERTEX_SHADER );
  shader->fragment_shader = BardGL_shader_compile( shader, GL_FRAGMENT_SHADER );

  shader->program = glCreateProgram();
  glAttachShader( shader->program, shader->vertex_shader );
  glAttachShader( shader->program, shader->fragment_shader );
  glLinkProgram( shader->program );

  GLint linkSuccess;
  glGetProgramiv( shader->program, GL_LINK_STATUS, &linkSuccess );
  if (linkSuccess == GL_FALSE) 
  {
    GLchar messages[256];
    glGetProgramInfoLog( shader->program, sizeof(messages), 0, &messages[0] );
    printf(">>>>>>>Shader Linker ERROR: %s", messages);
    // TODO: wrapper to print to Xcode console with NSLog
    exit(1);
  }

  shader->ready = TRUE;
  BardGL_use_builtin_shader( shader );

  return 0;
}

void BardGL_use_builtin_shader( BardGLShader* shader )
{
  GLuint _position;
  GLuint _color;
  GLuint _uv;
  GLuint _texture;

  // BardGLShader* shader;

  switch(shader->id)
  {
    case BARD_OPAQUE_SHADER:
      // shader = &BardGL_texture_shader;
      if (!shader->ready) {
        printf("Opaque Shader has not been compiled\n");
        return;
      }

      _position = BardGL_opaque_shader.attributes[BARD_BUILTIN_SHADER_ATTR_POSITION] = glGetAttribLocation( shader->program, "pos" );
      _color = BardGL_opaque_shader.attributes[BARD_BUILTIN_SHADER_ATTR_COLOR] = glGetAttribLocation( shader->program, "src_color" );
      glEnableVertexAttribArray( _position );
      glEnableVertexAttribArray( _color );

      BardGL_internal_render = &BardGL_render_with_builtin_opaque_shader;
      break;

    case BARD_TEXTURE_SHADER:
      // shader = &BardGL_opaque_shader;
      if (!shader->ready) {
        printf("Texture Shader has not been compiled\n");
        return;
      }

      _position = BardGL_texture_shader.attributes[BARD_BUILTIN_SHADER_ATTR_POSITION] = glGetAttribLocation( shader->program, "pos" );
      _color = BardGL_texture_shader.attributes[BARD_BUILTIN_SHADER_ATTR_COLOR] = glGetAttribLocation( shader->program, "src_color" );
      _uv = BardGL_texture_shader.attributes[BARD_BUILTIN_SHADER_ATTR_UV] = glGetAttribLocation( shader->program, "uv_in" );
      glEnableVertexAttribArray(_position);
      glEnableVertexAttribArray(_color);
      glEnableVertexAttribArray(_uv);

      _texture = BardGL_texture_shader.uniforms[BARD_BUILTIN_SHADER_UNIFORM_TEXTURE] = glGetUniformLocation(shader->program, "texture");

      BardGL_internal_render = &BardGL_render_with_builtin_texture_shader;
      break;

    case BARD_FIXEDCOLORTEXTURE_SHADER:
      // shader = &BardGL_fixed_color_texture_shader;
      if (!shader->ready) {
        printf("Fixed Color Texture Shader has not been compiled\n");
        return;
      }

      _position = BardGL_fixed_color_texture_shader.attributes[BARD_BUILTIN_SHADER_ATTR_POSITION] = glGetAttribLocation(shader->program, "pos");
      _color = BardGL_fixed_color_texture_shader.attributes[BARD_BUILTIN_SHADER_ATTR_COLOR] = glGetAttribLocation(shader->program, "src_color");
      _uv = BardGL_fixed_color_texture_shader.attributes[BARD_BUILTIN_SHADER_ATTR_UV] = glGetAttribLocation(shader->program, "uv_coords");
      glEnableVertexAttribArray(_position);
      glEnableVertexAttribArray(_color);
      glEnableVertexAttribArray(_uv);
      break;

    default:
      printf("Shader ID passed in does not match any existing shader IDs\n");
      exit(0);
  }

  shader->uniforms[BARD_BUILTIN_SHADER_UNIFORM_MODELVIEW] = glGetUniformLocation(shader->program, "model_view");
  shader->uniforms[BARD_BUILTIN_SHADER_UNIFORM_PROJECTION] = glGetUniformLocation(shader->program, "projection");

  glUseProgram(shader->program);
  BardGL_context.curr_shader = shader;
}

void BardGL_render_with_builtin_opaque_shader()
{
  BardGLShader* shader = BardGL_context.curr_shader;

  glUniformMatrix4fv( shader->uniforms[BARD_BUILTIN_SHADER_UNIFORM_MODELVIEW],
                      1, GL_FALSE, BardGL_context.model_view_matrix );
  glUniformMatrix4fv( shader->uniforms[BARD_BUILTIN_SHADER_UNIFORM_PROJECTION],
                      1, GL_FALSE, BardGL_context.projection_matrix );

  // TODO: create position and uv coord size constants instead of hardcoding "2"
  glVertexAttribPointer( shader->attributes[BARD_BUILTIN_SHADER_ATTR_POSITION],
                         2, GL_FLOAT, GL_FALSE, sizeof(BardGLVertex2), 0 );
  glVertexAttribPointer( shader->attributes[BARD_BUILTIN_SHADER_ATTR_COLOR], 4,
                         GL_UNSIGNED_BYTE, GL_FALSE, sizeof(BardGLVertex2),
                         (GLvoid*)(sizeof(GLfloat)*2*2) );
}

void BardGL_render_with_builtin_texture_shader()
{
  BardGLShader* shader = BardGL_context.curr_shader;

  glUniformMatrix4fv( shader->uniforms[BARD_BUILTIN_SHADER_UNIFORM_PROJECTION],
                      1, GL_FALSE, BardGL_context.projection_matrix );

  // TODO: create position and uv coord size constants instead of hardcoding "2"
  glVertexAttribPointer( shader->attributes[BARD_BUILTIN_SHADER_ATTR_POSITION],
                         2, GL_FLOAT, GL_FALSE, sizeof(BardGLVertex2), 0 );
  glVertexAttribPointer( shader->attributes[BARD_BUILTIN_SHADER_ATTR_COLOR], 4,
                         GL_UNSIGNED_BYTE, GL_FALSE, sizeof(BardGLVertex2),
                         (GLvoid*)(sizeof(GLfloat)*2*2) );
  glVertexAttribPointer( shader->attributes[BARD_BUILTIN_SHADER_ATTR_UV], 2,
                         GL_FLOAT, GL_FALSE, sizeof(BardGLVertex2),
                         (GLvoid*)(sizeof(GLfloat)*2) );

  glActiveTexture(GL_TEXTURE0);
  if (BardGL_active_texture) glBindTexture(GL_TEXTURE_2D, BardGL_active_texture->id);
  glUniform1i( shader->uniforms[BARD_BUILTIN_SHADER_UNIFORM_TEXTURE], 0);
}

// void BardGL_render_with_builtin_fixed_color_texture_shader();

void BardGL_load_ortho_projection( GLfloat left, GLfloat top,
                                 GLfloat right, GLfloat bottom,
                                 GLfloat near, GLfloat far )
{
  GLfloat r_l = right - left;
  GLfloat t_b = top - bottom;
  GLfloat f_n = far - near;
  GLfloat tx = - (right + left) / r_l;
  GLfloat ty = - (top + bottom) / t_b;
  GLfloat tz = - (far + near) / f_n;

  GLfloat mout[16] = {
    2.0f/r_l, 0.0f,      0.0f,     0.0f,
    0.0f,     2.0f/t_b,  0.0f,     0.0f,
    0.0f,     0.0f,     -2.0f/f_n, 0.0f,
    tx,       ty,       tz,        1.0f
  };

  BardGL_set_projection_matrix( mout );
}

// void BardGL_gltm_remove_texture( GLuint tex_id )
// void BardGL_gltm_remove_all( GLuint tex_id );
void BardGL_debug_print_vertices(BardGLContext* context)
{
  int i;
  for(i = 0; i < context->vertex_count; ++i)
  {
    printf("vertex %d: pos(%f, %f) ", i, context->vertex_list[i].position[0], context->vertex_list[i].position[1]);
    printf("uv(%f, %f) \n", context->vertex_list[i].uv_coords[0], context->vertex_list[i].uv_coords[1]);
  }
}

void BardGL_debug_print_indices(BardGLContext* context)
{
  int i;
  for(i = 0; i < context->index_count; ++i)
  {
    printf("index %d: %d \n", i, context->index_list[i]);
  }
}
