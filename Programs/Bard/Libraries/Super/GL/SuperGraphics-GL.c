#include "Super.h"
#include "SuperBitmap.h"
#include "SuperGraphics.h"

#if defined(SUPER_PLATFORM_MAC)
#  include <OpenGL/gl.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// TYPE DEFINITIONS
//-----------------------------------------------------------------------------
#ifdef SUPER_PLATFORM_MOBILE
#  define SUPER_PRECISION_MEDIUMP_FLOAT "precision mediump float;\n"
#  define SUPER_LOWP " lowp "
#else
#  define SUPER_PRECISION_MEDIUMP_FLOAT ""
#  define SUPER_LOWP " "
#endif

typedef struct SuperGraphicsGLVertexXY
{
  GLfloat x, y;
} SuperGraphicsGLVertexXY;

typedef struct SuperGraphicsGLVertexUV
{
  GLfloat u, v;
} SuperGraphicsGLVertexUV;


typedef struct SuperGraphicsTexture
{
  int    texture_id;
  GLuint gl_texture_id;
  int    width, height;
  int    texture_width, texture_height;
  int    bypp;  // bytes per pixel
  double max_u;
  double max_v;
} SuperGraphicsTexture;


typedef struct SuperGraphicsShader
{
  int vertex_shader;
  int pixel_shader;
  int program;

  int transform_setting;
  int position_attribute;
  int color_attribute;

  // Only valid for texturing shaders
  int texture_setting;
  int uv_attribute;
} SuperGraphicsShader;


//-----------------------------------------------------------------------------
// GLOBALS
// 
// This platform-specific data can't be stored in SuperGraphics_context.
//-----------------------------------------------------------------------------
int                     SuperGraphics_configured = 0;
SuperGraphicsGLVertexXY SuperGraphics_vertex_buffer_xy[SUPER_MAX_BUFFERED_VERTICES];
SuperGraphicsGLVertexUV SuperGraphics_vertex_buffer_uv[SUPER_MAX_BUFFERED_VERTICES];
GLint                   SuperGraphics_color_buffer[SUPER_MAX_BUFFERED_VERTICES];

SuperGraphicsShader*    SuperGraphics_color_shader = 0;
SuperGraphicsShader*    SuperGraphics_texture_shader = 0;
SuperGraphicsShader*    SuperGraphics_texture_shader_with_color_multiply = 0;
SuperGraphicsShader*    SuperGraphics_texture_shader_with_color_add      = 0;
SuperGraphicsShader*    SuperGraphics_premultiplied_texture_shader_with_color_add = 0;
SuperGraphicsShader*    SuperGraphics_texture_shader_with_color_fill     = 0;
SuperGraphicsShader*    SuperGraphics_premultiplied_texture_shader_with_color_fill = 0;

SuperResourceBank*      SuperGraphics_texture_bank = 0;


//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS
//-----------------------------------------------------------------------------
void SuperGraphics_check_for_shader_compile_errors( int shader )
{
  char buffer[256];
  GLsizei len;
  glGetShaderInfoLog( shader, 256, &len, (char*) buffer );
  if (len)
  {
    printf( "Error compiling shader: " );
    printf( "%s\n", buffer );
  }
}

SuperGraphicsShader* SuperGraphics_create_shader_info( const char* vertex_shader_src, const char* pixel_shader_src )
{
  SuperGraphicsShader* shader = Super_allocate( SuperGraphicsShader );

  shader->vertex_shader = glCreateShader( GL_VERTEX_SHADER );
  shader->pixel_shader = glCreateShader( GL_FRAGMENT_SHADER );

  glShaderSource( shader->vertex_shader, 1, (const char**) &vertex_shader_src, 0 );
  glCompileShader( shader->vertex_shader );
  SuperGraphics_check_for_shader_compile_errors( shader->vertex_shader );

  glShaderSource( shader->pixel_shader, 1, (const char**) &pixel_shader_src, 0 );
  glCompileShader( shader->pixel_shader );
  SuperGraphics_check_for_shader_compile_errors( shader->pixel_shader );

  shader->program = glCreateProgram();
  glAttachShader( shader->program, shader->vertex_shader );
  glAttachShader( shader->program, shader->pixel_shader );

  glLinkProgram( shader->program );

  shader->transform_setting  = glGetUniformLocation( shader->program, "transform" );
  shader->position_attribute = glGetAttribLocation( shader->program, "position" );
  shader->color_attribute    = glGetAttribLocation( shader->program, "color" );

  shader->texture_setting = glGetUniformLocation( shader->program, "texture" );
  shader->uv_attribute    = glGetAttribLocation( shader->program,  "uv" );

  return shader;
}

int SuperGraphics_create_texture( void* data, int w, int h, int flags )
{
  SuperGraphicsTexture* texture = Super_allocate( SuperGraphicsTexture );

  texture->texture_id = SuperResourceBank_add( SuperGraphics_texture_bank, texture, 0 );
  glGenTextures( 1, &texture->gl_texture_id );

  SuperGraphics_set_texture( texture->texture_id, w, h, data, flags );

  return texture->texture_id;
}


SuperGraphicsShader* SuperGraphics_destroy_shader_info( SuperGraphicsShader* shader )
{
  if ( !shader ) return 0;

  glDeleteShader( shader->vertex_shader );
  glDeleteShader( shader->pixel_shader );
  glDeleteProgram( shader->program );
  shader->vertex_shader = 0;
  shader->pixel_shader  = 0;
  shader->program = 0;

  return Super_free( shader );
}


void SuperGraphics_set_texture( int texture_id, int w, int h, void* data, int flags )
{
  SuperBitmap bitmap;
  SuperBitmap_init( &bitmap );

  SuperGraphicsTexture* texture = SuperResourceBank_get_resource( SuperGraphics_texture_bank, texture_id );
  if ( !texture ) return;

  SuperBitmap_borrow_data( &bitmap, data, w, h, 4 );

  if (flags & SUPER_TEXTURE_FORMAT_PREMULTIPLY_ALPHA)
  {
    SuperBitmap_promote( &bitmap );
    SuperBitmap_filter( &bitmap, SuperFilterFn_premultiply_alpha );
  }

  SuperBitmap_expand_to_power_of_two( &bitmap );
  SuperBitmap_swap_red_and_blue( &bitmap );

  texture->width  = w;
  texture->height = h;
  texture->texture_width  = bitmap.width;
  texture->texture_height = bitmap.height;
  texture->bypp = bitmap.bypp;

  glBindTexture( GL_TEXTURE_2D, texture->gl_texture_id );
  glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, texture->texture_width, texture->texture_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, bitmap.data );

  if (bitmap.owns_data)
  {
    SuperBitmap_release_data( &bitmap );
  }
  else
  {
    // Only bother to swap red and blue back if we're still operating on the original
    // image data.
    SuperBitmap_swap_red_and_blue( &bitmap );
  }

  texture->max_u = texture->width  / (double) texture->texture_width;
  texture->max_v = texture->height / (double) texture->texture_height;
}


void SuperGraphics_use_shader( SuperGraphicsShader* shader )
{
  GLfloat m[16];

  SuperMatrix_to_real32( &SuperGraphics.projection_transform, m );

  glUseProgram( shader->program );

  glUniformMatrix4fv( shader->transform_setting, 1, GL_FALSE, m );
  glVertexAttribPointer( shader->position_attribute, 2, GL_FLOAT, GL_FALSE, 0, SuperGraphics_vertex_buffer_xy );
  glEnableVertexAttribArray( shader->position_attribute );

  if (shader->color_attribute >= 0)
  {
    glVertexAttribPointer( shader->color_attribute, 4, GL_UNSIGNED_BYTE, GL_FALSE, 0, SuperGraphics_color_buffer );
    glEnableVertexAttribArray( shader->color_attribute );
  }

  if (shader->uv_attribute >= 0)
  {
    glVertexAttribPointer( shader->uv_attribute, 2, GL_FLOAT, GL_FALSE, 0, SuperGraphics_vertex_buffer_uv );
    glEnableVertexAttribArray( shader->uv_attribute );
  }

  if (shader->texture_setting >= 0)
  {
    glUniform1i( shader->texture_setting, 0 );
  }
}

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS
//-----------------------------------------------------------------------------
void SuperGraphics_configure()
{
  if (SuperGraphics_configured) return;
  SuperGraphics_configured = 1;

  SuperGraphicsDrawBuffer_init();
  SuperGraphics_texture_bank = SuperResourceBank_create();

  SuperGraphics_color_shader = SuperGraphics_create_shader_info(
      // Vertex Shader
      SUPER_PRECISION_MEDIUMP_FLOAT
      "uniform mat4   transform;                 \n"
      "attribute vec4 position;                  \n"
      "attribute" SUPER_LOWP "vec4 color;        \n"
      "varying  " SUPER_LOWP "vec4 vertex_color; \n"
      "void main()                               \n"
      "{                                         \n"
      "  gl_Position = transform * position;     \n"
      "  vertex_color = color / 255.0;           \n"
      "}                                         \n",

      // Pixel Shader
      SUPER_PRECISION_MEDIUMP_FLOAT
      "varying" SUPER_LOWP "vec4 vertex_color; \n"
      "void main()                             \n"
      "{                                       \n"
      "  gl_FragColor = vertex_color;          \n"
      "}                                       \n"
    );

  SuperGraphics_texture_shader = SuperGraphics_create_shader_info(
      // Vertex Shader
      SUPER_PRECISION_MEDIUMP_FLOAT
      "uniform mat4   transform;             \n"
      "attribute vec4 position;              \n"
      "attribute      vec2 uv;               \n"
      "varying        vec2 vertex_uv;        \n"
      "void main()                           \n"
      "{                                     \n"
      "  gl_Position = transform * position; \n"
      "  vertex_uv = uv;                     \n" 
      "}                                     \n",

      // Pixel Shader
      SUPER_PRECISION_MEDIUMP_FLOAT
      "uniform              sampler2D texture;        \n"
      "varying              vec2      vertex_uv;      \n"
      "void main()                                    \n"
      "{                                              \n"
      "  gl_FragColor = texture2D(texture,vertex_uv); \n"
      "}                                              \n"
    );

  SuperGraphics_texture_shader_with_color_multiply = SuperGraphics_create_shader_info(
      // Vertex Shader
      SUPER_PRECISION_MEDIUMP_FLOAT
      "uniform mat4   transform;                  \n"
      "attribute vec4 position;                   \n"
      "attribute      vec2 uv;                    \n"
      "varying        vec2 vertex_uv;             \n"
      "attribute" SUPER_LOWP "vec4 color;         \n"
      "varying  " SUPER_LOWP "vec4 vertex_color;  \n"
      "void main()                                \n"
      "{                                          \n"
      "  gl_Position = transform * position;      \n"
      "  vertex_uv = uv;                          \n" 
      "  vertex_color = color / 255.0;            \n"
      "}                                          \n",

      // Pixel Shader
      SUPER_PRECISION_MEDIUMP_FLOAT
      "uniform              sampler2D texture;                       \n"
      "varying              vec2      vertex_uv;                     \n"
      "varying" SUPER_LOWP "vec4 vertex_color;                       \n"
      "void main()                                                   \n"
      "{                                                             \n"
      "  gl_FragColor = texture2D(texture,vertex_uv) * vertex_color; \n"
      "}                                                             \n"
    );

  SuperGraphics_texture_shader_with_color_add = SuperGraphics_create_shader_info(
      // Vertex Shader
      SUPER_PRECISION_MEDIUMP_FLOAT
      "uniform mat4   transform;                 \n"
      "attribute vec4 position;                  \n"
      "attribute      vec2 uv;                   \n"
      "varying        vec2 vertex_uv;            \n"
      "attribute" SUPER_LOWP "vec4 color;        \n"
      "varying  " SUPER_LOWP "vec4 vertex_color; \n"
      "void main()                               \n"
      "{                                         \n"
      "  gl_Position = transform * position;     \n"
      "  vertex_uv = uv;                         \n" 
      "  vertex_color = color / 255.0;           \n"
      "}                                         \n",

      // Pixel Shader
      SUPER_PRECISION_MEDIUMP_FLOAT
      "uniform              sampler2D texture;                        \n"
      "varying              vec2      vertex_uv;                      \n"
      "varying" SUPER_LOWP "vec4 vertex_color;                        \n"
      "void main()                                                    \n"
      "{                                                              \n"
      "  gl_FragColor = texture2D(texture,vertex_uv) + vertex_color;  \n"
      "}                                                              \n"
    );

  SuperGraphics_premultiplied_texture_shader_with_color_add = SuperGraphics_create_shader_info(
      // Vertex Shader
      SUPER_PRECISION_MEDIUMP_FLOAT
      "uniform mat4   transform;                 \n"
      "attribute vec4 position;                  \n"
      "attribute      vec2 uv;                   \n"
      "varying        vec2 vertex_uv;            \n"
      "attribute" SUPER_LOWP "vec4 color;        \n"
      "varying  " SUPER_LOWP "vec4 vertex_color; \n"
      "void main()                               \n"
      "{                                         \n"
      "  gl_Position = transform * position;     \n"
      "  vertex_uv = uv;                         \n" 
      "  vertex_color = color / 255.0;           \n"
      "}                                         \n",

      // Pixel Shader
      SUPER_PRECISION_MEDIUMP_FLOAT
      "uniform              sampler2D texture;                       \n"
      "varying              vec2      vertex_uv;                     \n"
      "varying" SUPER_LOWP "vec4 vertex_color;                       \n"
      "void main()                                                   \n"
      "{                                                             \n"
      "  vec4 texture_color = texture2D(texture,vertex_uv);          \n"
      "  gl_FragColor = vec4( texture_color.xyz + (vertex_color.xyz * texture_color.a), texture_color.a ); \n"
      "}                                                             \n"
    );

  SuperGraphics_texture_shader_with_color_fill = SuperGraphics_create_shader_info(
      // Vertex Shader
      SUPER_PRECISION_MEDIUMP_FLOAT
      "uniform mat4   transform;                           \n"
      "attribute vec4 position;                            \n"
      "attribute      vec2 uv;                             \n"
      "varying        vec2 vertex_uv;                      \n"
      "attribute" SUPER_LOWP "vec4  color;                 \n"
      "varying  " SUPER_LOWP "vec4  vertex_color;          \n"
      "varying  " SUPER_LOWP "float vertex_inverse_a;      \n"
      "void main()                                         \n"
      "{                                                   \n"
      "  gl_Position = transform * position;               \n"
      "  vertex_uv = uv;                                   \n" 
      "  vertex_color = color / 255.0;                     \n"
      "  vertex_inverse_a = 1.0 - vertex_color.a;          \n"
      "}                                                   \n",

      // Pixel Shader
      SUPER_PRECISION_MEDIUMP_FLOAT
      "uniform                sampler2D texture;           \n"
      "varying                vec2      vertex_uv;         \n"
      "varying" SUPER_LOWP   "vec4      vertex_color;      \n"
      "varying  " SUPER_LOWP "float vertex_inverse_a;      \n"
      "void main()                                         \n"
      "{                                                   \n"
      "  vec4 texture_color = texture2D(texture,vertex_uv);        \n"
      "  gl_FragColor = vec4( (texture_color.xyz*vertex_inverse_a)+(vertex_color.xyz), texture_color.a );  \n" 
      "}                                                   \n"
    );

  SuperGraphics_premultiplied_texture_shader_with_color_fill = SuperGraphics_create_shader_info(
      // Vertex Shader
      SUPER_PRECISION_MEDIUMP_FLOAT
      "uniform mat4   transform;                            \n"
      "attribute vec4 position;                             \n"
      "attribute      vec2 uv;                              \n"
      "varying        vec2 vertex_uv;                       \n"
      "attribute" SUPER_LOWP "vec4  color;                  \n"
      "varying  " SUPER_LOWP "vec4  vertex_color;           \n"
      "varying  " SUPER_LOWP "float vertex_inverse_a;       \n"
      "void main()                                          \n"
      "{                                                    \n"
      "  gl_Position = transform * position;                \n"
      "  vertex_uv = uv;                                    \n" 
      "  vertex_color = color / 255.0;                      \n"
      "  vertex_inverse_a = 1.0 - vertex_color.a;           \n"
      "}                                                    \n",

      // Pixel Shader
      SUPER_PRECISION_MEDIUMP_FLOAT
      "uniform                sampler2D texture;            \n"
      "varying                vec2      vertex_uv;          \n"
      "varying" SUPER_LOWP   "vec4      vertex_color;       \n"
      "varying  " SUPER_LOWP "float vertex_inverse_a;       \n"
      "void main()                                          \n"
      "{                                                    \n"
      "  vec4 texture_color = texture2D(texture,vertex_uv); \n"
      "  gl_FragColor = vec4( ((texture_color.xyz*vertex_inverse_a)+vertex_color.xyz) * texture_color.a, texture_color.a );  \n" 
      "}                                                   \n"
    );
}

void SuperGraphics_retire()
{
  if ( !SuperGraphics_configured ) return;

  {
    // Free textures
    while (SuperResourceBank_count(SuperGraphics_texture_bank))
    {
      SuperGraphicsTexture* texture = SuperResourceBank_remove_first( SuperGraphics_texture_bank );
      glDeleteTextures( 1, &texture->gl_texture_id );
      Super_free( texture );
    }
    SuperGraphics_texture_bank = SuperResourceBank_destroy( SuperGraphics_texture_bank );
  }

  SuperGraphics_color_shader = SuperGraphics_destroy_shader_info( SuperGraphics_color_shader );
  SuperGraphics_texture_shader = SuperGraphics_destroy_shader_info( SuperGraphics_texture_shader );
  SuperGraphics_texture_shader_with_color_multiply = SuperGraphics_destroy_shader_info( SuperGraphics_texture_shader_with_color_multiply );
  SuperGraphics_texture_shader_with_color_add = SuperGraphics_destroy_shader_info( SuperGraphics_texture_shader_with_color_add );
  SuperGraphics_premultiplied_texture_shader_with_color_add = SuperGraphics_destroy_shader_info( SuperGraphics_premultiplied_texture_shader_with_color_add );
  SuperGraphics_texture_shader_with_color_fill = SuperGraphics_destroy_shader_info( SuperGraphics_texture_shader_with_color_fill );
  SuperGraphics_premultiplied_texture_shader_with_color_fill = SuperGraphics_destroy_shader_info( SuperGraphics_premultiplied_texture_shader_with_color_fill );

  SuperGraphics_configured = 0;
}

void SuperGraphics_begin_draw( int display_id, int display_width, int display_height )
{
  SuperGraphics_configure();

  SuperGraphics.display_id = display_id;
  SuperGraphics.display_width = display_width;
  SuperGraphics.display_height = display_height;

  glViewport( 0, 0, display_width, display_height );
}

void SuperGraphics_clear( int flags, int color, double depth, int stencil )
{
  SuperGraphics_flush();

  int gl_flags = 0;
  if (flags & SUPER_CLEAR_COLOR)
  {
    float a = ((color >> 24) & 255) / 255.0f;
    float r = ((color >> 16) & 255) / 255.0f;
    float g = ((color >> 8) & 255) / 255.0f;
    float b = (color & 255) / 255.0f;
    glClearColor( r, g, b, a );
    gl_flags |= GL_COLOR_BUFFER_BIT;
  }

  if (gl_flags) glClear( gl_flags );
}

void SuperGraphics_end_draw()
{
  SuperGraphics_flush();
  glFlush();
}


void SuperGraphics_flush()
{
  int render_mode;

  if ( !SuperGraphics.vertex_count )
  {
    return;
  }

  render_mode = SuperGraphics.render_mode;

  // ---- Set Up Blending Mode ------------------------------------------------
  if (render_mode & SUPER_RENDER_BLEND)
  {
    int gl_src_blend, gl_dest_blend;
    int src_blend  = ((render_mode>>8) & SUPER_BLEND_MASK);
    int dest_blend = ((render_mode>>12) & SUPER_BLEND_MASK);

    switch (src_blend)
    {
      case SUPER_BLEND_ZERO:              gl_src_blend = GL_ZERO;                break;
      case SUPER_BLEND_ONE:               gl_src_blend = GL_ONE;                 break;
      case SUPER_BLEND_SRC_ALPHA:         gl_src_blend = GL_SRC_ALPHA;           break;
      case SUPER_BLEND_INVERSE_SRC_ALPHA: gl_src_blend = GL_ONE_MINUS_SRC_ALPHA; break;
      default: 
        printf( "SuperGraphics-GL.c: Invalid src_blend %d\n", src_blend );
        gl_src_blend = GL_ONE;
    }

    switch (dest_blend)
    {
      case SUPER_BLEND_ZERO:              gl_dest_blend = GL_ZERO;                break;
      case SUPER_BLEND_ONE:               gl_dest_blend = GL_ONE;                 break;
      case SUPER_BLEND_SRC_ALPHA:         gl_dest_blend = GL_SRC_ALPHA;           break;
      case SUPER_BLEND_INVERSE_SRC_ALPHA: gl_dest_blend = GL_ONE_MINUS_SRC_ALPHA; break;
      default: 
        printf( "SuperGraphics-GL.c: Invalid dest_blend %d\n", dest_blend );
        gl_dest_blend = GL_ONE;
    }

    glEnable( GL_BLEND );
    glBlendFunc( gl_src_blend, gl_dest_blend );
  }
  else
  {
    glDisable( GL_BLEND );
  }

  // ---- Set Vertex Color Mode -----------------------------------------------
  // Copy vertex colors and set up vertex color mode
  SuperGraphicsVertex* src_vertex = SuperGraphics.vertices - 1;
  GLint*               dest_color = SuperGraphics_color_buffer - 1;
  int count = SuperGraphics.vertex_count;

  if (render_mode & SUPER_RENDER_ADJUST_VERTEX_COLORS)
  {
    // Swap red and blue AND multiply R,G,B by A.
    while (--count >= 0)
    {
      int color = (++src_vertex)->color;
      int a = (color >> 16) & 0xff00;
      int r = SuperGraphics.alpha_multiplication_lookup[ a | ((color >> 16) & 255) ];
      int g = SuperGraphics.alpha_multiplication_lookup[ a | ((color >> 8) & 255) ];
      int b = SuperGraphics.alpha_multiplication_lookup[ a | (color & 255) ];
      *(++dest_color) = (a << 16) | (b << 16) | (g << 8) | r;
    }
  }
  else
  {
    // Swap red and blue
    while (--count >= 0)
    {
      int color = (++src_vertex)->color;
      *(++dest_color) = (color & 0xff00ff00) | ((color >> 16) & 0xff) | ((color << 16) & 0xff0000);
    }
  }


  //printf("Rendering %d!\n", SuperGraphics.vertex_count);

  switch (SuperGraphics.draw_mode)
  {
    case SUPER_DRAW_LINES:
      {
        // Copy position data
        SuperGraphicsVertex*     src_vertex = SuperGraphics.vertices - 1;
        SuperGraphicsGLVertexXY* dest_pos   = SuperGraphics_vertex_buffer_xy - 1;
        int count = SuperGraphics.vertex_count;
        while (--count >= 0)
        {
          (++dest_pos)->x = (++src_vertex)->x;
          dest_pos->y     = src_vertex->y;
        }
      }

      SuperGraphics_use_shader( SuperGraphics_color_shader );
      glDrawArrays( GL_LINES, 0, SuperGraphics.vertex_count );

      break;

    case SUPER_DRAW_SOLID_TRIANGLES:
      {
        // Copy position data
        SuperGraphicsVertex*     src_vertex = SuperGraphics.vertices - 1;
        SuperGraphicsGLVertexXY* dest_pos   = SuperGraphics_vertex_buffer_xy - 1;
        int count = SuperGraphics.vertex_count;
        while (--count >= 0)
        {
          (++dest_pos)->x = (++src_vertex)->x;
          dest_pos->y     = src_vertex->y;
        }
      }

      SuperGraphics_use_shader( SuperGraphics_color_shader );
      glDrawArrays( GL_TRIANGLES, 0, SuperGraphics.vertex_count );
      break;

    case SUPER_DRAW_TEXTURED_TRIANGLES:
      // Set up texture
      {
        SuperGraphicsTexture* texture = SuperResourceBank_get_resource( SuperGraphics_texture_bank, SuperGraphics.texture_id );
        if ( !texture ) break;

        glActiveTexture( GL_TEXTURE0 );
        glEnable( GL_TEXTURE_2D );
        glBindTexture( GL_TEXTURE_2D, texture->gl_texture_id );

        // Texture Mode
        if (render_mode & SUPER_RENDER_TEXTURE_WRAP)
        {
          glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
          glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
        }
        else
        {
          glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
          glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        }

        if (render_mode & SUPER_RENDER_POINT_FILTER)
        {
          glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
          glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
        }
        else
        {
          glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
          glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
        }

        // Copy position and (u,v) data
        {
          double max_u = texture->max_u;
          double max_v = texture->max_v;
          SuperGraphicsVertex*     src_vertex = SuperGraphics.vertices - 1;
          SuperGraphicsGLVertexXY* dest_pos   = SuperGraphics_vertex_buffer_xy - 1;
          SuperGraphicsGLVertexUV* dest_uv    = SuperGraphics_vertex_buffer_uv - 1;
          int count = SuperGraphics.vertex_count;
          while (--count >= 0)
          {
            (++dest_pos)->x = (++src_vertex)->x;
            dest_pos->y     = src_vertex->y;
            (++dest_uv)->u  = src_vertex->u * max_u;
            dest_uv->v      = src_vertex->v * max_v;
          }
        }

        switch (render_mode & SUPER_VERTEX_COLOR_MODE_MASK)
        {
          case SUPER_VERTEX_COLOR_MODE_MULTIPLY:
            SuperGraphics_use_shader( SuperGraphics_texture_shader_with_color_multiply );
            break;

          case SUPER_VERTEX_COLOR_MODE_ADD:
            if ((render_mode & (SUPER_RENDER_BLEND|(SUPER_BLEND_MASK<<8)|(SUPER_BLEND_MASK<<12)))
                == (SUPER_RENDER_BLEND | (SUPER_BLEND_ONE<<8) | (SUPER_BLEND_INVERSE_SRC_ALPHA<<12)))
            {
              SuperGraphics_use_shader( SuperGraphics_premultiplied_texture_shader_with_color_add );
            }
            else
            {
              SuperGraphics_use_shader( SuperGraphics_texture_shader_with_color_add );
            }
            break;

          case SUPER_VERTEX_COLOR_MODE_FILL:
            if ((render_mode & (SUPER_RENDER_BLEND|(SUPER_BLEND_MASK<<8)|(SUPER_BLEND_MASK<<12)))
                == (SUPER_RENDER_BLEND | (SUPER_BLEND_ONE<<8) | (SUPER_BLEND_INVERSE_SRC_ALPHA<<12)))
            {
              SuperGraphics_use_shader( SuperGraphics_premultiplied_texture_shader_with_color_fill );
            }
            else
            {
              SuperGraphics_use_shader( SuperGraphics_texture_shader_with_color_fill );
            }
            break;

          default:
            SuperGraphics_use_shader( SuperGraphics_texture_shader );
        }

        glDrawArrays( GL_TRIANGLES, 0, SuperGraphics.vertex_count );

        glDisable( GL_TEXTURE_2D );
      }
      break;
  }

  SuperGraphics.vertex_count = 0;
}


int  SuperGraphics_get_texture_info( int texture_id, int* image_width_ptr, int* image_height_ptr, 
                                     int* texture_width_ptr, int* texture_height_ptr, int* bits_per_pixel_ptr )
{
  SuperGraphicsTexture* texture = SuperResourceBank_get_resource( SuperGraphics_texture_bank, texture_id );
  if ( !texture ) return 0;

  *image_width_ptr    = texture->width;
  *image_height_ptr   = texture->height;
  *texture_width_ptr  = texture->texture_width;
  *texture_height_ptr = texture->texture_height;
  *bits_per_pixel_ptr = texture->bypp * 8;

  return 1;
}

