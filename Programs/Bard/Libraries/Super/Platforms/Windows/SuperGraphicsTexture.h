#ifndef SUPER_GRAPHICS_TEXTURE
#define SUPER_GRAPHICS_TEXTURE

#include "SuperGraphics-DirectX.h"

struct SuperGraphicsTexture
{
  LPDIRECT3DDEVICE9  d3d_device;
  LPDIRECT3DTEXTURE9 d3d_texture;

  LPDIRECT3DSURFACE9 draw_target_surface;
  LPDIRECT3DSURFACE9 draw_target_backup;

  int  image_width;
  int  image_height;
  int  texture_width;
  int  texture_height;
  int  bits_per_pixel;

  int  flags;
  int  is_draw_target;

  SuperGraphicsTexture( LPDIRECT3DDEVICE9 device, int w, int h, int flags ) 
    : d3d_device(device),
      d3d_texture(NULL), draw_target_surface(NULL), draw_target_backup(NULL),
      image_width(0), image_height(0), texture_width(0), texture_height(0),
      bits_per_pixel(32),   // constant for now
      flags(flags), is_draw_target(0)
  {
    //all_textures.add(this);
    d3d_texture = NULL;
    draw_target_surface = NULL;
    draw_target_backup = NULL;
    this->is_draw_target = false;
    set_size( w, h, flags );
  }

  ~SuperGraphicsTexture()
  {
    destroy();
  }

  void destroy()
  {
    //all_textures.remove_value(this);

    if (draw_target_backup)
    {
      draw_target_backup->Release();
      draw_target_backup = NULL;
    }

    release();
  }

  void release()
  {
    if (draw_target_surface)
    {
      draw_target_surface->Release();
      draw_target_surface = NULL;
    }

    if (d3d_texture)
    {
      //mm.resource_bytes -= texture_width * texture_height * 4;
      d3d_texture->Release();
      d3d_texture = NULL;
    }
  }

  void set_size( int w, int h, int flags )
  {
    if (d3d_texture)
    {
      if (image_width == w  && image_height == h && (draw_target_surface!=0) == is_draw_target && this->flags == flags) return;
      release();
    }

    image_width  = w;
    image_height = h;
    this->flags = flags;

    if (FAILED(d3d_device->CreateTexture( 
        image_width, 
        image_height,
        1,  // mipmap levels
        is_draw_target ? D3DUSAGE_RENDERTARGET : 0,  // usage
        D3DFMT_A8R8G8B8,  // pixel format
        is_draw_target ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED,
        &d3d_texture,
        NULL
    )))
    {
      return;
    }

    if (is_draw_target)
    {
      d3d_texture->GetSurfaceLevel( 0, &draw_target_surface );
    }

    D3DSURFACE_DESC desc;
    d3d_texture->GetLevelDesc( 0, &desc );
    texture_width = desc.Width;
    texture_height = desc.Height;
  }

  void save_render_targets()
  {
    if ( !is_draw_target || draw_target_backup) return;

    if (SUCCEEDED(d3d_device->CreateOffscreenPlainSurface(
        texture_width, texture_height,
        D3DFMT_A8R8G8B8,  // pixel format
        D3DPOOL_SYSTEMMEM,
        &draw_target_backup,
        NULL )))
    {
      d3d_device->GetRenderTargetData( draw_target_surface, draw_target_backup );
    }
  }

  void release_render_targets()
  {
    if (is_draw_target) release();
  }

  void restore_render_targets()
  {
    if (is_draw_target && !d3d_texture)
    {
      set_size( image_width, image_height, is_draw_target );
      if (draw_target_backup)
      {
        d3d_device->UpdateSurface( draw_target_backup, NULL, draw_target_surface, NULL );
        draw_target_backup->Release();
        draw_target_backup = NULL;
      }
    }
  }

  void* lock()
  {
    D3DLOCKED_RECT lock_rect;
    if (D3D_OK == d3d_texture->LockRect( 0, &lock_rect, NULL, 0 ))
    {
      return lock_rect.pBits;
    }
    else
    {
      return NULL;
    }
  }

  void unlock()
  {
    d3d_texture->UnlockRect(0);
  }
};

#endif // SUPER_GRAPHICS_TEXTURE
