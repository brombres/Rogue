#include "SuperGraphics-DirectX.h"
#include "SuperUI-Windows.h"

#include <stdio.h>

#include "SuperGraphicsDrawBuffer.h"

//-----------------------------------------------------------------------------
// Prototypes
//-----------------------------------------------------------------------------
LPDIRECT3DDEVICE9 SuperGraphics_prepare_d3d_device( SuperUIWindowInfo* window_info );


//-----------------------------------------------------------------------------
// GLOBALS
//-----------------------------------------------------------------------------
int                     SuperGraphics_configured     = 0;
int                     SuperGraphics_is_full_screen = 0;
LPDIRECT3D9             SuperGraphics_d3d        = NULL;  // the DirectX graphics library 
LPDIRECT3DDEVICE9       SuperGraphics_device = NULL;
HWND                    SuperGraphics_current_window = NULL;
SuperGraphicsDrawBuffer SuperGraphics_draw_buffer;
SuperResourceBank*      SuperGraphics_textures = NULL;

double SuperGraphics_projection_scale_x = 1.0;
double SuperGraphics_projection_scale_y = 1.0;

IDirect3DSurface9* SuperGraphics_backbuffer = NULL;

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS
//-----------------------------------------------------------------------------
void SuperGraphics_configure()
{
  if (SuperGraphics_configured) return;

  OutputDebugString( "---- Configuring SuperGraphics ----\n" );

  SuperGraphics_textures = SuperResourceBank_create();

  SuperGraphics_d3d = Direct3DCreate9( D3D_SDK_VERSION );
  if ( !SuperGraphics_d3d ) OutputDebugString( "**** Failed to create Direct3D object ****\n" );

  SuperGraphics_configure_font_system();

  SuperGraphics_prepare_d3d_device( NULL );

  SuperGraphics_configured = 1;
}

void SuperGraphics_retire()
{
  SuperGraphics_retire_font_system();
  SuperGraphics_textures = SuperResourceBank_destroy( SuperGraphics_textures );
}

void SuperGraphics_draw_window( SuperUIWindowInfo* info )
{
  //char st[120];
  //sprintf( st, "---- SuperGraphics_draw_window %dx%d ----\n", info->width, info->height );
  //OutputDebugString( st );

  LPDIRECT3DDEVICE9 device = SuperGraphics_prepare_d3d_device( info );
  if ( !device ) return;

  SuperGraphics_current_window = info->window_handle;

  int w = info->width;
  int h = info->height;

  SuperGraphics_projection_scale_x = 2.0 / GetSystemMetrics(SM_CXFULLSCREEN);
  SuperGraphics_projection_scale_y = 2.0 / GetSystemMetrics(SM_CYFULLSCREEN);

  SuperGraphics_draw_buffer.begin_draw( info );


  SuperEvent event;
  event.type = SUPER_EVENT_DRAW;
  event.draw = { info->window_id, w, h, 0, 0, w, h };
  SuperEventSystem_dispatch( &event );

  // Rendering of scene objects happens here
  // 1. Set the vertex decleration
  // 2. Set the stream source
  //device->SetVertexDeclaration(vertexDecleration);
  //device->SetStreamSource(0, vertexBuffer, 0, sizeof(POSCOLORVERTEX));
  //device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);

  SuperGraphics_draw_buffer.end_draw();

  RECT r = {0,0,info->width,info->height};
  device->Present(&r,&r,info->window_handle,0);
  //device->Present(NULL,NULL,info->window_handle,0);
  //OutputDebugString( "---- SuperGraphics_draw_window ----\n" );
}

void SuperGraphics_clear( int flags, int color, double depth, int stencil )
{
  int dx_flags = 0;
  D3DCOLOR dx_color = 0;

  if ( !SuperGraphics_device ) return;

  SuperGraphics_flush();

  if (flags & SUPER_CLEAR_COLOR)
  {
    int a = (color >> 24) & 255;
    int r = (color >> 16) & 255;
    int g = (color >>  8) & 255;
    int b = color & 255;
    dx_flags |= D3DCLEAR_TARGET;
    dx_color = D3DCOLOR_ARGB(a,r,g,b);
  }

  if (dx_flags) SuperGraphics_device->Clear( 0, 0, dx_flags, dx_color, 1.0f, 0 );
}

void SuperGraphics_clip( int x, int y, int w, int h )
{
  if ( !SuperGraphics_device ) return;

  SuperGraphics_flush();

  RECT r = {x,y,x+w,y+h};
  SuperGraphics_device->SetScissorRect( &r );
}

int SuperGraphics_create_texture( int w, int h, int* data, int flags )
{
  if ( !SuperGraphics_device ) return 0;

  bool render_target = (data == NULL);
  SuperGraphicsTexture* texture = new SuperGraphicsTexture( SuperGraphics_device, w, h, flags );

  int texture_id = SuperResourceBank_add( SuperGraphics_textures, texture, NULL );

  SuperGraphics_set_texture( texture_id, w, h, data, flags );

  return texture_id;
}

void SuperGraphics_destroy_texture( int texture_id )
{
  if ( !SuperGraphics_device ) return;

  SuperGraphicsTexture* texture = (SuperGraphicsTexture*) SuperResourceBank_remove( SuperGraphics_textures, texture_id );

  if (texture)
  {
    SuperGraphics_flush();
    delete texture;
  }
}

void SuperGraphics_draw_line( double x1, double y1, double x2, double y2, int color )
{
  if ( !SuperGraphics_device ) return;

  SuperGraphics_draw_buffer.set_line_mode();
  SuperGraphics_draw_buffer.add( SuperGraphicsVertex2D((float)x1,(float)y1,color) );
  SuperGraphics_draw_buffer.add( SuperGraphicsVertex2D((float)x2,(float)y2,color) );
}

void SuperGraphics_fill_solid_triangle( double x1, double y1, int color1, double x2, double y2, int color2, double x3, double y3, int color3 )
{
  if ( !SuperGraphics_device ) return;
  SuperGraphics_draw_buffer.set_solid_triangle_mode();
  SuperGraphics_draw_buffer.add( SuperGraphicsVertex2D((float)x1,(float)y1,color1) );
  SuperGraphics_draw_buffer.add( SuperGraphicsVertex2D((float)x2,(float)y2,color2) );
  SuperGraphics_draw_buffer.add( SuperGraphicsVertex2D((float)x3,(float)y3,color3) );
}

void SuperGraphics_fill_textured_triangle( double x1, double y1, double u1, double v1, int color1,
                                           double x2, double y2, double u2, double v2, int color2,
                                           double x3, double y3, double u3, double v3, int color3,
                                           int texture_id )
{
  if ( !SuperGraphics_device ) return;

  SuperGraphicsTexture* texture = (SuperGraphicsTexture*) SuperResourceBank_get_resource( SuperGraphics_textures, texture_id );
  if ( !texture ) return;

  x1 -= 0.5;
  y1 -= 0.5;
  x2 -= 0.5;
  y2 -= 0.5;
  x3 -= 0.5;
  y3 -= 0.5;

  SuperGraphics_draw_buffer.set_textured_triangle_mode( texture, NULL );
  SuperGraphics_draw_buffer.add( SuperGraphicsVertex2D((float)x1,(float)y1,color1,(float)u1,(float)v1) );
  SuperGraphics_draw_buffer.add( SuperGraphicsVertex2D((float)x2,(float)y2,color2,(float)u2,(float)v2) );
  SuperGraphics_draw_buffer.add( SuperGraphicsVertex2D((float)x3,(float)y3,color3,(float)u3,(float)v3) );
}

void SuperGraphics_flush()
{
  if ( !SuperGraphics_device ) return;
  SuperGraphics_draw_buffer.render();
}

int SuperGraphics_get_texture_info( int texture_id, int* image_width_ptr, int* image_height_ptr, 
                                    int* texture_width_ptr, int* texture_height_ptr, int* bits_per_pixel_ptr )
{
  SuperGraphicsTexture* texture;

  *image_width_ptr = *image_height_ptr = *texture_width_ptr = *texture_height_ptr = *bits_per_pixel_ptr = 0;

  texture = (SuperGraphicsTexture*) SuperResourceBank_get_resource( SuperGraphics_textures, texture_id );
  if ( !texture ) return 0;

  *image_width_ptr = texture->image_width;
  *image_height_ptr = texture->image_height;
  *texture_width_ptr = texture->texture_width;
  *texture_height_ptr = texture->texture_height;
  *bits_per_pixel_ptr = texture->bits_per_pixel;

  return 1;
}

void SuperGraphics_set_render_mode( int render_mode )
{
  SuperGraphics_draw_buffer.set_render_flags( render_mode | (SUPER_BLEND_ONE<<8) | (SUPER_BLEND_INVERSE_SRC_ALPHA<<12) );
}

void SuperGraphics_set_texture( int texture_id, int w, int h, int* data, int flags )
{
  if ( !SuperGraphics_device ) return;

  SuperGraphicsTexture* texture = (SuperGraphicsTexture*) SuperResourceBank_get_resource( SuperGraphics_textures, texture_id );
  if ( !texture ) return;

  SuperGraphics_flush();

  if (data)
  {
    texture->set_size( w, h, flags );

    int* dest_line_start = (int*) texture->lock();
    if (dest_line_start)
    {
      int  pitch = texture->texture_width;
      int  lines = h;
      int* src_line_start = data;

      if (flags & SUPER_TEXTURE_FORMAT_PREMULTIPLY_ALPHA)
      {
        --dest_line_start;
        --src_line_start;
        while (--lines >= 0)
        {
          int* dest = dest_line_start;
          int* src = src_line_start;
          int count = w;
          while (--count >= 0)
          {
            int color = *(++src);
            int a = (color >> 24) & 255;
            int r = (((color >> 16) & 255) * a) / 255;
            int g = (((color >> 8) & 255) * a) / 255;
            int b = ((color & 255) * a) / 255;
            *(++dest) = (a<<24) | (r<<16) | (g<<8) | b;
          }
          dest_line_start += pitch;
          src_line_start  += w;
        }
      }
      else
      {
        int  bytes_per_row = w * 4;
        while (--lines >= 0)
        {
          memcpy( dest_line_start, src_line_start, bytes_per_row );
          dest_line_start += pitch;
          src_line_start  += w;
        }
      }

      texture->unlock();
    }
  }
}

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS
//-----------------------------------------------------------------------------
LPDIRECT3DDEVICE9 SuperGraphics_prepare_d3d_device( SuperUIWindowInfo* window_info )
{
  D3DPRESENT_PARAMETERS device_params;
  LPDIRECT3DDEVICE9 device = SuperGraphics_device;

  SuperGraphics_device = device;
  
  bool needs_reset = false;
  if (device)
  {
    HRESULT status = device->TestCooperativeLevel();
    if (status != D3D_OK)
    {
      SuperGraphics_draw_buffer.release();
      if (status == D3DERR_DEVICELOST) return NULL;

      if (status == D3DERR_DEVICENOTRESET)
      {
        needs_reset = true;
      }
      else
      {
        device->Release();
        device = NULL;
        SuperGraphics_device = NULL;
      }
    }
  }

  if ( !device || needs_reset )
  {
    // Create or Reset
    ZeroMemory( &device_params, sizeof(device_params) );
    device_params.BackBufferFormat = D3DFMT_UNKNOWN;

    // TODO: only do the following if the window is resizable
    device_params.BackBufferWidth  = GetSystemMetrics( SM_CXFULLSCREEN );
    device_params.BackBufferHeight = GetSystemMetrics( SM_CYFULLSCREEN );

    device_params.Windowed = 1;
    //device_params.SwapEffect = D3DSWAPEFFECT_DISCARD;
    device_params.SwapEffect = D3DSWAPEFFECT_COPY;
    device_params.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    if (device)
    {
      if (FAILED(device->Reset(&device_params))) return NULL;
    }
    else
    {
      if (FAILED(SuperGraphics_d3d->CreateDevice( 
              D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, GetDesktopWindow(), //window_info->window_handle,
              D3DCREATE_MIXED_VERTEXPROCESSING,
              &device_params, &device ))) return NULL;
    }

    SuperGraphics_device = device;
  }

  device->SetRenderState( D3DRS_CULLMODE, D3DCULL_NONE ); 

  //Turn on Alpha blending (get some transparencies goin on)
  device->SetRenderState(D3DRS_ALPHABLENDENABLE,  true);

  device->SetSamplerState( 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
  device->SetSamplerState( 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );
  device->SetSamplerState( 1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
  device->SetSamplerState( 1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );

  // Turn off D3D lighting
  device->SetRenderState( D3DRS_LIGHTING, false );

  // Turn off the zbuffer
  device->SetRenderState( D3DRS_ZENABLE, false );

  // local var w  = bounds.width
  // local var h  = bounds.y1 - bounds.y2
  // local var tx = -(bounds.x1 + bounds.x2) / w
  // local var ty = -(bounds.y1 + bounds.y2) / h
  // local var tz = near / (near - far)
  // return Matrix4(
  //   2/w,   0,             0, 0,
  //     0, 2/h,             0, 0,
  //     0,   0,  1/(far-near), 0,
  //    tx,  ty,            tz, 1 )

  float sw = (float)GetSystemMetrics( SM_CXFULLSCREEN );
  float sh = (float)GetSystemMetrics( SM_CYFULLSCREEN );
  D3DMATRIX transform =
  {
    2.0f/sw,    0.0f,  0.0f, 0.0f,
    0.0f   ,-2.0f/sh,  0.0f, 0.0f,
    0.0f   ,    0.0f, -1.0f, 0.0f,
   -1.0f   ,    1.0f,  0.0f, 1.0f
  };

  device->SetTransform( D3DTS_PROJECTION, (D3DMATRIX*) &transform );

  if (window_info)
  {
    RECT r = {0,0,window_info->width,window_info->height};
    device->SetScissorRect( &r );
  }
  device->SetRenderState( D3DRS_SCISSORTESTENABLE, true );

  return device;
}

