//
//  BardGL_glcore.h
//  BardRendererTest
//
//  Created by Michael Dwan on 8/9/13.
//  Copyright (c) 2013 AdColony. All rights reserved.
//

#ifndef BardRendererTest_BardGL_glcore_h
#define BardRendererTest_BardGL_glcore_h

#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>

#include "Bard.h"

#ifndef NULL
#define NULL (0)
#endif

#ifndef BOOL
#define BOOL int
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define BARD_SHADER_FLAG_BLEND   0x4 // Alpha Blending
#define BARD_SHADER_FLAG_FCM     0x2 // Fixed Color Multiplication
#define BARD_SHADER_FLAG_TEXTURE 0x1 // Texture

// Combinations of the above three flags. Used to choose a built-in shader.
                                            // BMT
#define BARD_OPAQUE_SHADER                0 // 000
#define BARD_TEXTURE_SHADER               1 // 001
#define BARD_MULTIPLY_SHADER              2 // 010
#define BARD_FIXEDCOLORTEXTURE_SHADER     3 // 011

#define BARD_BUILTIN_SHADER_ATTR_POSITION 0
#define BARD_BUILTIN_SHADER_ATTR_COLOR    1
#define BARD_BUILTIN_SHADER_ATTR_UV       2

#define BARD_BUILTIN_SHADER_UNIFORM_MODELVIEW  0
#define BARD_BUILTIN_SHADER_UNIFORM_PROJECTION 1
#define BARD_BUILTIN_SHADER_UNIFORM_TEXTURE    2

#define BARD_VERTEX_LIST_SIZE 4096
#define BARD_INDEX_LIST_SIZE  BARD_VERTEX_LIST_SIZE/4*6 // 4 verts and 6 indices per "sprite"

#define BARD_DRAW_MODE_POINTS             0
#define BARD_DRAW_MODE_LINES              1
#define BARD_DRAW_MODE_TRIANGLES          2
#define BARD_DRAW_MODE_TEXTURED_TRIANGLES 3
// #define BARD_DRAW_MODE_FCM_TEXTURED_TRIANGLES 3

#define BARD_TEXTURE_FILTER_LINEAR           0
#define BARD_TEXTURE_FILTER_NEAREST_NEIGHBOR 1

#define BLEND_ZERO           0
#define BLEND_ONE            1
#define BLEND_SRC_ALPHA      2
#define BLEND_INV_SRC_ALPHA  3
#define BLEND_DEST_ALPHA     4
#define BLEND_INV_DEST_ALPHA 5
#define BLEND_DEST_COLOR     6
#define BLEND_INV_DEST_COLOR 7
#define BLEND_OPAQUE         8
#define BLEND_SRC_COLOR      9
#define BLEND_INV_SRC_COLOR  10


typedef struct BardGLVertex2
{
  GLfloat position[2];
  GLfloat uv_coords[2];
  GLuint argb;
} BardGLVertex2;

typedef struct BardGLTexture
{
  int    resource_id;
  GLuint id;
  GLuint framebuffer;
  int image_width;
  int image_height;
  int texture_width;
  int texture_height;
} BardGLTexture;

extern BardResourceBank BardGL_texture_bank;
extern BardGLTexture* BardGL_active_texture;

typedef struct BardGLShader
{
  int id;
  BOOL ready;
  char* vertex_src;
  char* fragment_src;
  GLuint vertex_shader;
  GLuint fragment_shader;
  int program;

  GLint uniforms[8];
  GLint attributes[8];
} BardGLShader;

typedef struct BardMatrixStack {
  int sp;
  BOOL changed;
  float el[64][16];
} BardMatrixStack;

typedef struct BardGLContext
{
  int draw_mode;
  int render_flags;
  int display_width;
  int display_height;

  BOOL blending_enabled;
  GLenum src_blend;
  GLenum dest_blend;

  BardGLTexture* active_texture;
  BOOL texturing_enabled;
  GLenum texture_filter;
  unsigned int constant_color;

  GLfloat projection_matrix[16];
  GLfloat model_view_matrix[16];
  BardMatrixStack view_matrix_stack;

  int vertex_count;
  BardGLVertex2 vertex_list[BARD_VERTEX_LIST_SIZE];
  int index_count;
  short index_list[BARD_INDEX_LIST_SIZE];

  GLuint framebuffer;
  GLuint color_renderbuffer;
  GLuint vertex_buffer;
  GLuint index_buffer;

  BardGLShader* curr_shader;
} BardGLContext;


extern BardGLContext BardGL_context;

typedef void(*BardGL_render_ptr)(void);


extern BardGLShader BardGL_opaque_shader;
extern BardGLShader BardGL_texture_shader;
extern BardGLShader BardGL_fixed_color_texture_shader;

#define SEPARATE_ARGB32(c,a,r,g,b) a=(c>>24)&255; r=(c>>16)&255; g=(c>>8)&255; b=c&255
#define COMBINE_ARGB32(a,r,g,b) ((a<<24)|(r<<16)|(g<<8)|b)


short BardGL_add_vertex_2d( double x, double y, unsigned int aaRRggBB,
                    double u, double v, BOOL index );
void BardGL_add_vertex_index( short i );
void BardGL_begin_draw( int display_width, int display_height );
void BardGL_clear( unsigned int aaRRggBB );
void BardGL_clear_view_transform();
void BardGL_draw_line_2d( double x1, double y1, double x2, double y2, unsigned int aaRRggBB );
void BardGL_draw_triangle_2d( double x1, double y1, double x2, double y2, double x3, double y3,
                       double u1, double v1, double u2, double v2, double u3, double v3,
                       unsigned int aaRRggBB );
void BardGL_draw_box( double x1, double y1, double x2, double y2,
                      double u1, double v1, double u2, double v2,
                      unsigned int aaRRggBB );

void BardGL_draw_box_rotated( double xc, double yc, double w, double h,
                       double u1, double v1, double u2, double v2,
                       unsigned int aaRRggBB, double radians);

void BardGL_enable_alpha_blending( BOOL setting );
void BardGL_enable_fixed_color( unsigned int aaRRggBB );

void BardGL_enable_texture( BOOL setting );
void BardGL_end_draw();

int BardGL_get_display_width();
int BardGL_get_display_height();

BardGLTexture* BardGL_get_texture( int resource_id );

int BardGL_get_image_width( int texture_id );
int BardGL_get_image_height( int texture_id );
int BardGL_get_texture_width( int texture_id );
int BardGL_get_texture_height( int texture_id );
int BardGL_get_pixels_per_inch();
BOOL BardGL_is_tablet();

int BardGL_load_texture( char* filepath );
void BardGL_push_view_matrix( GLfloat m[16] );
void BardGL_push_view_transform( double width, double height,
                          double center_x, double center_y, double radians,
                          double scale_x, double scale_y, BOOL hflip, BOOL vflip );
void BardGL_pop_view_matrix();
void BardGL_render();
void BardGL_set_point_rendering_mode();
void BardGL_set_line_rendering_mode();
void BardGL_set_projection_matrix( GLfloat m[16] );
void BardGL_set_triangle_rendering_mode();
void BardGL_set_textured_triangle_rendering_mode();
void BardGL_set_texture_wrap( int w, int h );
void BardGL_unload();
void BardGL_use_blend_mode( GLenum src_blend, GLenum dest_blend );
void BardGL_use_alpha_source( int texture_id );
void BardGL_use_linear_filter();
void BardGL_use_nearest_neighbor_filter();
void BardGL_use_texture( int texture_id, int stage );


void BardGL_init();
void BardGL_init_gl_renderbuffer();
void BardGL_init_gl_framebuffer();
void BardGL_init_gl_vertex_and_index_buffers();
GLuint BardGL_shader_compile( BardGLShader* shader, GLenum shader_type );
int  BardGL_shader_build( BardGLShader* shader );
void BardGL_use_builtin_shader( BardGLShader* shader );


void BardGL_load_ortho_projection( GLfloat left, GLfloat top,
                                 GLfloat right, GLfloat bottom,
                                 GLfloat near, GLfloat far );

void BardGL_render_with_builtin_opaque_shader();
void BardGL_render_with_builtin_texture_shader();
void BardGL_render_with_builtin_fixed_color_texture_shader();

// void BardGL_gltm_remove_texture( GLuint tex_id );
// void BardGL_gltm_remove_all( GLuint tex_id );

void BardGL_debug_print_vertices(BardGLContext* context);
void BardGL_debug_print_indices(BardGLContext* context);

#endif //BardRendererTest_BardGL_glcore_h
