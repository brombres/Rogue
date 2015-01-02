#ifndef SUPER_GRAPHICS_DRAW_BUFFER_H
#define SUPER_GRAPHICS_DRAW_BUFFER_H

#include "SuperGraphics-DirectX.h"
#include "SuperGraphicsTexture.h"

#include <d3d9.h>
#include <stdio.h>


//=============================================================================
//  SuperGraphicsVertex2D
//=============================================================================
#define SUPER_VERTEX_FVF2D (D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX2)

struct SuperGraphicsVertex2D
{
  FLOAT x, y, z;        // The transformed position for the vertex
  DWORD color;          // The vertex color 0xAARRGGBB
  FLOAT tu, tv;         // Texture coordinates 
  FLOAT alpha_tu, alpha_tv; 
    // 2nd pair of tex coords 
    // (used only when rendering with a separate alpha texture)

  SuperGraphicsVertex2D() { }
  SuperGraphicsVertex2D( FLOAT _x, FLOAT _y ) : x(_x), y(_y), z(0.0f) { }
  SuperGraphicsVertex2D( FLOAT _x, FLOAT _y, DWORD _color ) : x(_x), y(_y), z(0.0f), color(_color)  { }

  SuperGraphicsVertex2D( FLOAT _x, FLOAT _y, DWORD _color, FLOAT _tu, FLOAT _tv )
      : x(_x), y(_y), z(0.0f), color(_color), tu(_tu), tv(_tv) 
  { 
    // 
  }

  SuperGraphicsVertex2D( FLOAT _x, FLOAT _y, DWORD _color, FLOAT _tu, FLOAT _tv, FLOAT atu, FLOAT atv )
      : x(_x), y(_y), z(0.0f), color(_color), tu(_tu), tv(_tv), alpha_tu(atu), alpha_tv(atv) { }

  void premultiply_alpha()
  {
    int a = (color >> 24) & 255;
    int r = (((color >> 16) & 255) * a) / 255;
    int g = (((color >> 8) & 255) * a) / 255;
    int b = ((color & 255) * a) / 255;
    color = (a<<24) | (r<<16) | (g<<8) | b;
  }


  //  // 2D transform
  //  Matrix4 xform = plasmacore.view_transform;
  //  float new_x = (float)(xform.r1c1 * x + xform.r1c2 * y + xform.r1c4);
  //  float new_y = (float)(xform.r2c1 * x + xform.r2c2 * y + xform.r2c4);
  //  x = new_x;
  //  y = new_y;
  //}
};


//=============================================================================
//  SuperGraphicsDrawBuffer
//=============================================================================
struct SuperGraphicsDrawBuffer
{
  LPDIRECT3DDEVICE9       device;
  LPDIRECT3DVERTEXBUFFER9 vertices;

  int draw_mode;
  int count;
  int render_flags;
  int src_blend, dest_blend;
  SuperGraphicsVertex2D* vertex_pos;
  DWORD constant_color;

  SuperGraphicsTexture* texture;
  SuperGraphicsTexture* alpha_src;
  //SuperGraphicsTexture* draw_target;

  SuperGraphicsDrawBuffer()
  {
    vertices = NULL;
    draw_mode = SUPER_DRAW_TEXTURED_TRIANGLES;
    count = 0;
    vertex_pos = 0;
    texture = NULL;
    alpha_src = NULL;
    //draw_target = NULL; 
    render_flags = 0;
    src_blend = SUPER_BLEND_ONE;
    dest_blend = SUPER_BLEND_INVERSE_SRC_ALPHA;
  }

  ~SuperGraphicsDrawBuffer()
  {
    release();
  }

  void begin_draw( SuperUIWindowInfo* window_info )
  {
    device = SuperGraphics_device;

    // Calculate constants to convert coordinates [0,width]x[0,height] into [-1,1]x[-1,1]

    if ( !vertices )
    {
      device->CreateVertexBuffer( 
          sizeof(SuperGraphicsVertex2D) * SUPER_MAX_BUFFERED_VERTICES,
          0,
          SUPER_VERTEX_FVF2D,
          D3DPOOL_MANAGED,
          &vertices,
          NULL );
      lock();
    }

    device->BeginScene();
  }

  void end_draw()
  {
    render();
    device->EndScene();
  }

  void release()
  {
    if (vertices)
    {
      unlock();
      vertices->Release();
      vertices = NULL;
    }
  }

  void lock()
  {
    vertices->Lock( 0, SUPER_MAX_BUFFERED_VERTICES * sizeof(SuperGraphicsVertex2D), (void**) &vertex_pos, 0 );
  }

  void unlock()
  {
    vertices->Unlock();
  }

  void set_render_flags( int flags )
  {
    //if (render_flags != flags)
    {
      render();
      render_flags = flags;
      this->src_blend = (render_flags >> 8) & SUPER_BLEND_MASK;
      this->dest_blend = (render_flags >> 12) & SUPER_BLEND_MASK;
      //if (log_drawing) LOG( "  [changing render flags]" );
    }
  }

  void set_textured_triangle_mode( SuperGraphicsTexture* texture, SuperGraphicsTexture* alpha_src )
  {
    if (draw_mode != SUPER_DRAW_TEXTURED_TRIANGLES || this->texture != texture || this->alpha_src != alpha_src)
    {
      render();
      draw_mode = SUPER_DRAW_TEXTURED_TRIANGLES;
      this->texture = texture;
      this->alpha_src = alpha_src;
    }
  }

  void set_solid_triangle_mode()
  {
    if (draw_mode != SUPER_DRAW_SOLID_TRIANGLES) render();
    draw_mode = SUPER_DRAW_SOLID_TRIANGLES;
  }

  void set_line_mode()
  {
    if (draw_mode != SUPER_DRAW_LINES) render();
    draw_mode = SUPER_DRAW_LINES;
  }
  
  void set_point_mode()
  {
    if (draw_mode != SUPER_DRAW_POINTS) render();
    draw_mode = SUPER_DRAW_POINTS;
  }

  /*
  void set_draw_target( SuperGraphicsTexture* target )
  {
    if (draw_target == target) return;

    if (log_drawing) LOG( "  [set_draw_target]" );

    if (draw_target)
    {
      device->EndScene();
    }

    draw_target = target;

    if (draw_target) 
    {
      device->SetRenderTarget( 0, draw_target->draw_target_surface );
      device->BeginScene();
    }
    else 
    {
      device->SetRenderTarget( 0, backbuffer );
      device->SetRenderState( D3DRS_ALPHABLENDENABLE, true );
    }
  }
  */
  
  void add( SuperGraphicsVertex2D v1, SuperGraphicsVertex2D v2, SuperGraphicsVertex2D v3 )
  {
    if (count == SUPER_MAX_BUFFERED_VERTICES) render();

    //if ((render_flags & SUPER_RENDER_OPTION_ADJUST_VERTEX_COLORS) && constant_color != v1.color)
    //{
    //  render();
    //  constant_color = v1.color;
    //}

    if ( (render_flags & SUPER_RENDER_OPTION_ADJUST_VERTEX_COLORS) )
    {
      v1.premultiply_alpha();
      v2.premultiply_alpha();
      v3.premultiply_alpha();
    }

    vertex_pos[0] = v1;
    vertex_pos[1] = v2;
    vertex_pos[2] = v3;
    vertex_pos += 3;
    count += 3;
  }

  void add( SuperGraphicsVertex2D v1, SuperGraphicsVertex2D v2 )
  {
    if (count == SUPER_MAX_BUFFERED_VERTICES) render();

    if ( (render_flags & SUPER_RENDER_OPTION_ADJUST_VERTEX_COLORS) )
    {
      v1.premultiply_alpha();
      v2.premultiply_alpha();
    }

    vertex_pos[0] = v1;
    vertex_pos[1] = v2;
    vertex_pos += 2;
    count += 2;
  }

  void add( SuperGraphicsVertex2D v )
  {
    if (count == SUPER_MAX_BUFFERED_VERTICES) render();

    if ( (render_flags & SUPER_RENDER_OPTION_ADJUST_VERTEX_COLORS) )
    {
      v.premultiply_alpha();
    }

    *(vertex_pos++) = v;
    ++count;
  }

  void render()
  {
    if ( !count ) return;

    unlock();

    D3DBLEND src_factor, dest_factor;
    switch (src_blend)
    {
      case SUPER_BLEND_ZERO:
        src_factor = D3DBLEND_ZERO;
        break;
      case SUPER_BLEND_ONE:
        src_factor = D3DBLEND_ONE;
        break;
      case SUPER_BLEND_SRC_ALPHA:
        src_factor = D3DBLEND_SRCALPHA;
        break;
      case SUPER_BLEND_INVERSE_SRC_ALPHA:
        src_factor = D3DBLEND_INVSRCALPHA;
        break;
      default:
        src_factor = D3DBLEND_ONE;
    }

    switch (dest_blend)
    {
      case SUPER_BLEND_ZERO:
        dest_factor = D3DBLEND_ZERO;
        break;
      case SUPER_BLEND_ONE:
        dest_factor = D3DBLEND_ONE;
        break;
      case SUPER_BLEND_SRC_ALPHA:
        dest_factor = D3DBLEND_SRCALPHA;
        break;
      case SUPER_BLEND_INVERSE_SRC_ALPHA:
        dest_factor = D3DBLEND_INVSRCALPHA;
        break;
      default:
        dest_factor = D3DBLEND_INVSRCALPHA;
    }

    device->SetRenderState( D3DRS_SRCBLEND,  src_factor );
    device->SetRenderState( D3DRS_DESTBLEND, dest_factor );

    switch (draw_mode)
    {
      case SUPER_DRAW_TEXTURED_TRIANGLES:
        // set colorization flags
        device->SetTexture( 0, texture->d3d_texture );

        //if (render_flags & SUPER_RENDER_OPTION_ADJUST_VERTEX_COLORS)
        //{
        //  device->SetRenderState( D3DRS_TEXTUREFACTOR, constant_color );
        //  device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR );
        //  device->SetRenderState( D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA );
        //  device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
        //}
        //else
        {
          device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
        }

        // set texture addressing to WRAP or CLAMP
        if (render_flags & SUPER_RENDER_OPTION_TEXTURE_WRAP)
        {
          device->SetSamplerState( 0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP );
          device->SetSamplerState( 0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP );
        }
        else
        {
          device->SetSamplerState( 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
          device->SetSamplerState( 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );
        }

        if (render_flags & SUPER_RENDER_OPTION_POINT_FILTER)
        {
          device->SetSamplerState( 0, D3DSAMP_MINFILTER, D3DTEXF_POINT );
          device->SetSamplerState( 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT );
        }
        else
        {
          device->SetSamplerState( 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR );
          device->SetSamplerState( 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR );
        }

        if (alpha_src) 
        {
          device->SetTexture( 1, alpha_src->d3d_texture );

          device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1 );
          device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE );

          // grab color from first stage
          device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
          device->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_CURRENT );

          // use the alpha from this stage
          device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
          device->SetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

          device->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1 );
        }
        else
        {
          device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE );
          device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE );
          device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
        }


        device->SetStreamSource( 0, vertices, 0, sizeof(SuperGraphicsVertex2D) );
        device->SetFVF( SUPER_VERTEX_FVF2D );
        device->DrawPrimitive( D3DPT_TRIANGLELIST, 0, count/3 );   

        device->SetTexture( 0, NULL );
        device->SetTexture( 1, NULL );
        break;

      case SUPER_DRAW_SOLID_TRIANGLES:
        //if (log_drawing) LOG( "  drawing %d solid triangles", (count/3) );
        device->SetStreamSource( 0, vertices, 0, sizeof(SuperGraphicsVertex2D) );
        device->SetFVF( SUPER_VERTEX_FVF2D );
        device->DrawPrimitive( D3DPT_TRIANGLELIST, 0, count/3 );   
        break;

      case SUPER_DRAW_LINES:
        //if (log_drawing) LOG( "  drawing %d lines", (count/2) );
        device->SetStreamSource( 0, vertices, 0, sizeof(SuperGraphicsVertex2D) );
        device->SetFVF( SUPER_VERTEX_FVF2D );
        device->DrawPrimitive( D3DPT_LINELIST, 0, count/2 );   
        break;

      case SUPER_DRAW_POINTS:
        //if (log_drawing) LOG( "  drawing %d points", (count) );
        device->SetStreamSource( 0, vertices, 0, sizeof(SuperGraphicsVertex2D) );
        device->SetFVF( SUPER_VERTEX_FVF2D );
        device->DrawPrimitive( D3DPT_POINTLIST, 0, count );
        break;
    }

    count = 0;
    lock();
  }
};

#endif // SUPER_GRAPHICS_DRAW_BUFFER_H

