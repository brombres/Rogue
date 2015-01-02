#include "SuperBardVM.h"

#include "Super.h"
#include "SuperUI.h"
#include "SuperGraphics.h"

#ifdef SUPER_PLATFORM_WINDOWS
#  include "SuperUI-Windows.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// PROTOTYPES
//=============================================================================
void BardGraphics_native_clear( BardVM* vm );
void BardGraphics_native_create_texture( BardVM* vm );
void BardGraphics_native_set_clip( BardVM* vm );
void BardGraphics_native_destroy_texture( BardVM* vm );
void BardGraphics_native_draw_line( BardVM* vm );
void BardGraphics_native_fill_triangle( BardVM* vm );
void BardGraphics_native_fill_textured_triangle( BardVM* vm );
void BardGraphics_native_get_texture_info( BardVM* vm );
void BardGraphics_native_load_bitmap_from_png( BardVM* vm );
void BardGraphics_native_load_font( BardVM* vm );
void BardGraphics_native_load_texture_from_png( BardVM* vm );
void BardGraphics_native_render_font_character( BardVM* vm );
void BardGraphics_native_set_render_mode( BardVM* vm );
void BardGraphics_native_set_texture( BardVM* vm );

void BardWindow_native_content_size( BardVM* vm );
void BardWindow_native_create( BardVM* vm );
void BardWindow_native_show( BardVM* vm );

void SuperBardVM_event_handler( SuperEvent* e );


//=============================================================================
// GLOBALS
//=============================================================================
int          SuperBardVM_argc = 0;
const char** SuperBardVM_argv = NULL;


//=============================================================================
// PUBLIC METHODS
//=============================================================================
BardVM* SuperBardVM_create()
{

  BardVM* vm = BardVM_create();

  SuperEventSystem_configure( SuperBardVM_event_handler, vm );
  SuperUI_configure();
  SuperGraphics_configure();

  BardStandardLibrary_configure( vm );

  SuperBardVM_configure( vm );

  return vm;
}

BardVM* SuperBardVM_destroy( BardVM* vm )
{
  return BardVM_destroy( vm );
}

void SuperBardVM_set_args( int argc, const char* argv[] )
{
  SuperBardVM_argc = argc;
  SuperBardVM_argv = argv;
}

void SuperBardVM_configure( BardVM* vm )
{
  BardVM_register_native_method( vm, "Graphics", "native_clear(Integer)", BardGraphics_native_clear );
  BardVM_register_native_method( vm, "Graphics", "native_create_texture(Integer,Integer,Array<<Integer>>,Integer)", BardGraphics_native_create_texture );
  BardVM_register_native_method( vm, "Graphics", "native_set_clip(Real,Real,Real,Real)", BardGraphics_native_set_clip );
  BardVM_register_native_method( vm, "Graphics", "native_destroy_texture(Integer)", BardGraphics_native_destroy_texture );
  BardVM_register_native_method( vm, "Graphics", "native_draw_line(Real,Real,Real,Real,Integer)", BardGraphics_native_draw_line );
  BardVM_register_native_method( vm, "Graphics", "native_fill_triangle(Real,Real,Integer,Real,Real,Integer,Real,Real,Integer)",
      BardGraphics_native_fill_triangle );
  BardVM_register_native_method( vm, "Graphics",
      "native_fill_triangle(Real,Real,Real,Real,Integer,Real,Real,Real,Real,Integer,Real,Real,Real,Real,Integer,Integer)",
      BardGraphics_native_fill_textured_triangle );
  BardVM_register_native_method( vm, "Graphics", "native_get_texture_info(Integer)", BardGraphics_native_get_texture_info );
  BardVM_register_native_method( vm, "Graphics", "native_load_bitmap_from_png(String)", BardGraphics_native_load_bitmap_from_png );
  BardVM_register_native_method( vm, "Graphics", "native_load_font(String)", BardGraphics_native_load_font );
  BardVM_register_native_method( vm, "Graphics", "native_load_texture_from_png(String,Integer)", BardGraphics_native_load_texture_from_png );
  BardVM_register_native_method( vm, "Graphics", "native_render_font_character(Integer,Character,Integer,Integer)", 
      BardGraphics_native_render_font_character );
  BardVM_register_native_method( vm, "Graphics", "native_set_render_mode(Integer)", BardGraphics_native_set_render_mode );
  BardVM_register_native_method( vm, "Graphics", "native_set_texture(Integer,Integer,Integer,Array<<Integer>>,Integer)", BardGraphics_native_set_texture );

  BardVM_register_native_method( vm, "Window", "native_content_size(Integer)", BardWindow_native_content_size );
  BardVM_register_native_method( vm, "Window", "native_create(String,Integer,Integer,Integer,Integer,Integer)", BardWindow_native_create );
  BardVM_register_native_method( vm, "Window", "native_show(Integer,Logical)", BardWindow_native_show );
}

int SuperBardVM_execute( BardVM* vm, const char* filepath )
{
#ifdef SUPER_PLATFORM_WINDOWS
  int running = 1;
  MSG message;

  SuperBardVM_launch( vm, filepath );


  while (running)
  {
    while (PeekMessage(&message, NULL, 0, 0, PM_REMOVE))
    {
      if (message.message == WM_QUIT)
      {
        running = 0;
      }
      else
      {
        TranslateMessage(&message);
        DispatchMessage(&message);
      }
      Sleep(1);  // Not necessary if vsync works correctly but can't hurt
    }

    SuperUI_draw_all_windows();
  }

  return (int) message.wParam;
#else
  SuperBardVM_launch( vm, filepath );

  return 0;
#endif
}

void SuperBardVM_launch( BardVM* vm, const char* bc_filepath )
{
  BardVM_load_bc_file( vm, bc_filepath );
  BardVM_add_command_line_arguments( vm, SuperBardVM_argc, SuperBardVM_argv );

  BardVM_create_main_object( vm );
}

#ifdef SUPER_PLATFORM_WINDOWS
void SuperBardVM_configure_windows( HINSTANCE hInstance, HINSTANCE hPrevInstance, const char* lpCmdLine, int nCmdShow )
{
  SuperUI_configure_windows( hInstance, hPrevInstance, lpCmdLine, nCmdShow );
}
#endif

//-----------------------------------------------------------------------------
//  Graphics
//-----------------------------------------------------------------------------
void BardGraphics_native_clear( BardVM* vm )
{
  BardInteger argb = BardVM_pop_integer( vm );

  // Leave 'this' on stack

  SuperGraphics_clear( SUPER_CLEAR_COLOR, argb, 0, 0 );
}

void BardGraphics_native_create_texture( BardVM* vm )
{
  int flags = BardVM_pop_integer( vm );
  int* data = NULL;
  BardArray* obj = (BardArray*) BardVM_pop_object( vm );
  int h = BardVM_pop_integer( vm );
  int w = BardVM_pop_integer( vm );
  BardVM_pop_discard( vm );

  if (obj) data = obj->integer_data;

  BardVM_push_integer( vm, SuperGraphics_create_texture(w, h, data, flags) );
}

void BardGraphics_native_set_clip( BardVM* vm )
{
  int h = (int) BardVM_pop_real( vm );
  int w = (int) BardVM_pop_real( vm );
  int y = (int) BardVM_pop_real( vm );
  int x = (int) BardVM_pop_real( vm );
  BardVM_pop_discard( vm );

  SuperGraphics_clip( x, y, w, h );
}

void BardGraphics_native_destroy_texture( BardVM* vm )
{
  int texture_id = BardVM_pop_integer( vm );
  BardVM_pop_discard( vm );

  SuperGraphics_destroy_texture( texture_id );
}

void BardGraphics_native_draw_line( BardVM* vm )
{
  BardInteger color = BardVM_pop_integer( vm );
  BardReal y2 = BardVM_pop_real( vm );
  BardReal x2 = BardVM_pop_real( vm );
  BardReal y1 = BardVM_pop_real( vm );
  BardReal x1 = BardVM_pop_real( vm );
  BardVM_pop_discard( vm );

  SuperGraphics_draw_line( x1, y1, x2, y2, color );
}

void BardGraphics_native_fill_triangle( BardVM* vm )
{
  BardInteger color3 = BardVM_pop_integer( vm );
  BardReal y3 = BardVM_pop_real( vm );
  BardReal x3 = BardVM_pop_real( vm );
  BardInteger color2 = BardVM_pop_integer( vm );
  BardReal y2 = BardVM_pop_real( vm );
  BardReal x2 = BardVM_pop_real( vm );
  BardInteger color1 = BardVM_pop_integer( vm );
  BardReal y1 = BardVM_pop_real( vm );
  BardReal x1 = BardVM_pop_real( vm );
  BardVM_pop_discard( vm );

  SuperGraphics_fill_solid_triangle( x1, y1, color1, x2, y2, color2, x3, y3, color3 );
}

void BardGraphics_native_fill_textured_triangle( BardVM* vm )
{
  int texture_id = BardVM_pop_integer( vm );
  BardInteger color3 = BardVM_pop_integer( vm );
  BardReal v3 = BardVM_pop_real( vm );
  BardReal u3 = BardVM_pop_real( vm );
  BardReal y3 = BardVM_pop_real( vm );
  BardReal x3 = BardVM_pop_real( vm );
  BardInteger color2 = BardVM_pop_integer( vm );
  BardReal v2 = BardVM_pop_real( vm );
  BardReal u2 = BardVM_pop_real( vm );
  BardReal y2 = BardVM_pop_real( vm );
  BardReal x2 = BardVM_pop_real( vm );
  BardInteger color1 = BardVM_pop_integer( vm );
  BardReal v1 = BardVM_pop_real( vm );
  BardReal u1 = BardVM_pop_real( vm );
  BardReal y1 = BardVM_pop_real( vm );
  BardReal x1 = BardVM_pop_real( vm );
  BardVM_pop_discard( vm );

  SuperGraphics_fill_textured_triangle( x1, y1, u1, v1, color1, x2, y2, u2, v2, color2, x3, y3, u3, v3, color3, texture_id );
}

void BardGraphics_native_get_texture_info( BardVM* vm )
{
  int image_width=0, image_height=0, texture_width=0, texture_height=0, bpp=0;

  int texture_id = BardVM_pop_integer( vm );
  BardVM_pop_discard( vm );

  SuperGraphics_get_texture_info( texture_id, &image_width, &image_height, &texture_width, &texture_height, &bpp );

  BardVM_push_integer( vm, bpp );
  BardVM_push_integer( vm, texture_height );
  BardVM_push_integer( vm, texture_width );
  BardVM_push_integer( vm, image_height );
  BardVM_push_integer( vm, image_width );
}


void BardGraphics_native_load_bitmap_from_png( BardVM* vm )
{
  BardObject* filepath_obj = BardVM_pop_object( vm );
  BardVM_pop_discard( vm );

  char  filepath[512];
  int width, height;
  SuperInteger* data;
  int success = 0;

  BardString_to_c_string( (BardString*) filepath_obj, filepath, 512 );
  data = SuperGraphics_load_pixel_data_from_png_file( filepath, &width, &height );

  if (data)
  {
    BardType* type_IntegerArray = BardVM_find_type( vm, "Array<<Integer>>" );
    BardType* type_Integer = BardVM_find_type( vm, "Integer" );
    BardType* type_Bitmap  = BardVM_find_type( vm, "Bitmap" );
    if (type_IntegerArray && type_Integer && type_Bitmap)
    {
      BardMethod* m_init_bitmap = BardType_find_method( type_Bitmap, "create(Integer,Integer,Array<<Integer>>)" );
      if (m_init_bitmap)
      {
        success = 1;
        BardArray* array = BardArray_create_of_type( vm, type_IntegerArray, type_Integer, width*height );
        memcpy( array->integer_data, data, width*height*sizeof(SuperInteger) );

        BardVM_push_object( vm, BardType_create_object(type_Bitmap) );
        BardVM_push_integer( vm, width );
        BardVM_push_integer( vm, height );
        BardVM_push_object( vm, array );
        BardVM_invoke( vm, type_Bitmap, m_init_bitmap );
      }
    }

    SUPER_FREE( data );
  }

  if ( !success ) BardVM_push_object( vm, NULL );
}


void BardGraphics_native_load_font( BardVM* vm )
{
  BardObject* filepath_obj = BardVM_pop_object( vm );
  BardVM_pop_discard( vm );
  char  filepath[512];

  BardString_to_c_string( (BardString*) filepath_obj, filepath, 512 );

  BardVM_push_integer( vm, SuperGraphics_load_font(filepath) );
}


void BardGraphics_native_load_texture_from_png( BardVM* vm )
{
  int flags = BardVM_pop_integer( vm );
  BardObject* filepath_obj = BardVM_pop_object( vm );
  BardVM_pop_discard( vm );
  char  filepath[512];

  BardString_to_c_string( (BardString*) filepath_obj, filepath, 512 );

  BardVM_push_integer( vm, SuperGraphics_create_texture_from_png_file(filepath,flags) );
}


void BardGraphics_native_render_font_character( BardVM* vm )
{
  SuperByte* data;
  int offset_x, offset_y, advance_x, advance_y, pitch;

  int pixel_height = BardVM_pop_integer( vm );
  int pixel_width = BardVM_pop_integer( vm );
  int unicode = BardVM_pop_character( vm );
  int font_id = BardVM_pop_integer( vm );
  BardVM_pop_discard( vm );

  data = SuperGraphics_render_font_character( font_id, unicode, &pixel_width, &pixel_height, &offset_x, &offset_y, &advance_x, &advance_y, &pitch );

  {
    BardType* type_ByteArray = BardVM_find_type( vm, "Array<<Byte>>" );
    BardType* type_Byte      = BardVM_find_type( vm, "Byte" );
    BardType* type_AlphaBitmap = BardVM_find_type( vm, "AlphaBitmap" );
    BardType* type_FontCharacterData = BardVM_find_type( vm, "FontCharacterData" );
    if (type_ByteArray && type_Byte && type_AlphaBitmap && type_FontCharacterData)
    {
      BardMethod* m_init_bitmap = BardType_find_method( type_AlphaBitmap, "init(Integer,Integer,Array<<Byte>>,Array<<Integer>>)" );
      BardMethod* m_init_data = BardType_find_method( type_FontCharacterData, "init(AlphaBitmap,XY,XY)" );
      if (m_init_bitmap && m_init_data)
      {
        // Start FontCharacterData object creation...
        BardVM_push_object( vm, BardType_create_object(type_FontCharacterData) );

        if (data)
        {
          // ...with an AlphaBitmap as the first arg
          // copy pixel data
          int count = pixel_width * pixel_height;
          BardArray* array = BardArray_create_of_type( vm, type_ByteArray, type_Byte, count );
          BardByte* dest = array->byte_data;
          SuperByte* src = data;
          int lines = pixel_height;
          while (--lines >= 0)
          {
            memcpy( dest, src, pixel_width );
            src += pitch;
            dest += pixel_width;
          }

          BardVM_push_object( vm, BardType_create_object(type_AlphaBitmap) );
          BardVM_push_integer( vm, pixel_width );
          BardVM_push_integer( vm, pixel_height );
          BardVM_push_object( vm, array );
          BardVM_push_object( vm, NULL );
          BardVM_invoke( vm, type_AlphaBitmap, m_init_bitmap );
        }
        else
        {
          // No bitmap data but spacing info may still be valid
          BardVM_push_object( vm, NULL );
        }

        // Other FontCharacterData args
        BardVM_push_real( vm, offset_y );
        BardVM_push_real( vm, offset_x );
        BardVM_push_real( vm, advance_y );
        BardVM_push_real( vm, advance_x );
        BardVM_invoke( vm, type_FontCharacterData, m_init_data );
        return;
      }
    }
  }

  BardVM_push_object( vm, NULL );
}


void BardGraphics_native_set_render_mode( BardVM* vm )
{
  int mode = BardVM_pop_integer( vm );
  BardVM_pop_discard( vm );

  SuperGraphics_set_render_mode( mode );
}

void BardGraphics_native_set_texture( BardVM* vm )
{
  int flags = BardVM_pop_integer( vm );
  BardArray* obj = (BardArray*) BardVM_pop_object( vm );
  int h = BardVM_pop_integer( vm );
  int w = BardVM_pop_integer( vm );
  int texture_id = BardVM_pop_integer( vm );
  BardVM_pop_discard( vm );

  if (obj)
  {
    SuperGraphics_set_texture( texture_id, w, h, obj->integer_data, flags );
  }
}



//-----------------------------------------------------------------------------
//  Window
//-----------------------------------------------------------------------------
void BardWindow_native_content_size( BardVM* vm )
{
  int width, height;

  BardInteger window_id = BardVM_pop_integer( vm );
  BardVM_pop_discard( vm );

  SuperUI_get_window_content_size( window_id, &width, &height );

  // Returns XY(w,h)
  BardVM_push_real( vm, height );
  BardVM_push_real( vm, width );
}

void BardWindow_native_create( BardVM* vm )
{
  BardInteger flags = BardVM_pop_integer( vm );
  BardInteger h = BardVM_pop_integer( vm );
  BardInteger w = BardVM_pop_integer( vm );
  BardInteger y = BardVM_pop_integer( vm );
  BardInteger x = BardVM_pop_integer( vm );
  BardObject* title_obj = BardVM_pop_object( vm );
  BardVM_pop_discard( vm );
  char  title_buffer[80];
  char* title = NULL;

  if (title_obj)
  {
    title = title_buffer;
    BardString_to_c_string( (BardString*) title_obj, title_buffer, 80 );
  }

  BardVM_push_integer( vm, SuperUI_create_window(title,x,y,w,h,flags,vm) );
}

void BardWindow_native_show( BardVM* vm )
{
  int setting = BardVM_pop_logical( vm );
  int window_id = BardVM_pop_integer( vm );
  BardVM_pop_discard( vm );

  SuperUI_show_window( window_id, setting );
}

//-----------------------------------------------------------------------------
//  CALLBACKS
//-----------------------------------------------------------------------------
void SuperBardVM_event_handler( SuperEvent* e )
{
  BardVM* vm = (BardVM*) e->context;

  switch (e->type)
  {
    case SUPER_EVENT_DRAW:
      {
        BardVM_update( vm );  // get an UPDATE in before the draw

        BardVM_write_event_integer( vm, BARD_EVENT_DRAW );
        BardVM_write_event_integer( vm, e->draw.window_id );
        BardVM_write_event_integer( vm, e->draw.display_width );
        BardVM_write_event_integer( vm, e->draw.display_height );
        BardVM_write_event_integer( vm, e->draw.clip_x );
        BardVM_write_event_integer( vm, e->draw.clip_y );
        BardVM_write_event_integer( vm, e->draw.clip_width );
        BardVM_write_event_integer( vm, e->draw.clip_height );
        BardVM_dispatch_events( vm );
        break;
      }

    case SUPER_EVENT_POINTER_FOCUS:
      {
        BardVM_write_event_integer( vm, BARD_EVENT_POINTER_FOCUS );
        BardVM_write_event_integer( vm, e->pointer_focus.window_id );
        BardVM_write_event_integer( vm, e->pointer_focus.focus_gained );
        break;
      }

    case SUPER_EVENT_POINTER_MOVEMENT:
      {
        BardVM_write_event_integer( vm, BARD_EVENT_POINTER_ACTION );
        BardVM_write_event_integer( vm, BARD_EVENT_POINTER_ACTION_MOVEMENT );
        BardVM_write_event_integer( vm, e->pointer_movement.window_id );
        BardVM_write_event_real( vm, e->pointer_movement.x );
        BardVM_write_event_real( vm, e->pointer_movement.y );
        BardVM_write_event_integer( vm, 0 );
        break;
      }

    case SUPER_EVENT_POINTER_PRESS:
      {
        BardVM_write_event_integer( vm, BARD_EVENT_POINTER_ACTION );
        BardVM_write_event_integer( vm, BARD_EVENT_POINTER_ACTION_PRESS );
        BardVM_write_event_integer( vm, e->pointer_press.window_id );
        BardVM_write_event_real( vm, e->pointer_press.x );
        BardVM_write_event_real( vm, e->pointer_press.y );
        BardVM_write_event_integer( vm, e->pointer_press.button_index );
        break;
      }

    case SUPER_EVENT_POINTER_RELEASE:
      {
        BardVM_write_event_integer( vm, BARD_EVENT_POINTER_ACTION );
        BardVM_write_event_integer( vm, BARD_EVENT_POINTER_ACTION_RELEASE );
        BardVM_write_event_integer( vm, e->pointer_release.window_id );
        BardVM_write_event_real( vm, e->pointer_release.x );
        BardVM_write_event_real( vm, e->pointer_release.y );
        BardVM_write_event_integer( vm, e->pointer_release.button_index );
        break;
      }

    case SUPER_EVENT_KEY_PRESS:
      {
        BardVM_write_event_integer( vm, BARD_EVENT_KEY_ACTION );
        if (e->key_press.is_repeat) BardVM_write_event_integer( vm, BARD_EVENT_KEY_ACTION_REPEAT );
        else                        BardVM_write_event_integer( vm, BARD_EVENT_KEY_ACTION_PRESS );
        BardVM_write_event_integer( vm, e->key_press.window_id );
        BardVM_write_event_integer( vm, e->key_press.unicode );
        BardVM_write_event_integer( vm, e->key_press.keycode );
        BardVM_write_event_integer( vm, e->key_press.syscode );
        break;
      }

    case SUPER_EVENT_KEY_RELEASE:
      {
        BardVM_write_event_integer( vm, BARD_EVENT_KEY_ACTION );
        BardVM_write_event_integer( vm, BARD_EVENT_KEY_ACTION_RELEASE );
        BardVM_write_event_integer( vm, e->key_press.window_id );
        BardVM_write_event_integer( vm, e->key_press.unicode );
        BardVM_write_event_integer( vm, e->key_press.keycode );
        BardVM_write_event_integer( vm, e->key_press.syscode );
        break;
      }
  }
}

