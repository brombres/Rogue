//
//  SuperAppAppDelegate.m
//  SuperApp
//
//  Created by Abe Pralle on 5/3/14.
//  Copyright (c) 2014 Plasmaworks. All rights reserved.
//

#import  "SuperAppAppDelegate.h"
#include "SuperEventSystem.h"
#include "SuperFontSystem.h"
#include "SuperGraphics.h"

#include <stdio.h>

int    loaded = 0;
int    img1 = 0;
int    img2 = 0;
double mouse_x, mouse_y;

void draw()
{
  int w = SuperGraphics.display_width;
  int h = SuperGraphics.display_height;

  if ( !loaded )
  {
    int data[16] = 
    {
      0xffff0000, 0xffaa0000, 0xff550000, 0xff000000,
      0xa0ff0000, 0xa0aa0000, 0xa0550000, 0xa0000000,
      0x50ff0000, 0x50aa0000, 0x50550000, 0x50000000,
      0x00ff0000, 0x00aa0000, 0x00550000, 0x00000000,
    };
    //img1 = SuperGraphics_create_texture_from_file( "HatersGonnaHate.png", 0 );
    img1 = SuperGraphics_create_texture( data, 4, 4, 0 );
    img2 = SuperGraphics_create_texture( data, 4, 4, SUPER_TEXTURE_FORMAT_PREMULTIPLY_ALPHA );
    printf( "image index: %d\n", img1 );

    int font_face = SuperFontSystem_load_font_face( Super_locate_asset("", "arial.ttf") );
    printf( "FONT FACE: %d\n", font_face );

    int w=0,h=200,dx,dy,ax,ay;
    /*
    SuperInteger* data = SuperFontSystem_render_character( font_face, 'A', &w, &h, &dx, &dy, &ax, &ay );
    if (data)
    {
      //img1 = SuperGraphics_create_texture( data, w, h, 0 );
    }
    else
    {
      printf( "no font data returned\n");
    }
    */

    loaded = 1;
  }

  SuperGraphics_clear( SUPER_CLEAR_COLOR, 0xff000080, 0, 0 );

  SuperMatrix m;
  SuperMatrix_orthographic( &m, w, h, 10 );
  SuperGraphics_set_projection_transform( &m );

  SuperGraphics_set_render_mode( SuperGraphics_create_render_mode(SUPER_RENDER_POINT_FILTER|SUPER_RENDER_BLEND,SUPER_BLEND_SRC_ALPHA,SUPER_BLEND_INVERSE_SRC_ALPHA,SUPER_VERTEX_COLOR_MODE_ADD) );

  int color = 0x00404040;

  SuperGraphics_fill_textured_box( 0,    0, w>>1, h>>1, 0, 0, 1, 1, color, img1 );
  //SuperGraphics_fill_textured_box( 0, h>>1, w>>1, h>>1, 0, 0, 1, 1, color, img2 );

  SuperGraphics_set_render_mode( SuperGraphics_create_render_mode(SUPER_RENDER_POINT_FILTER|SUPER_RENDER_BLEND,SUPER_BLEND_ONE,SUPER_BLEND_INVERSE_SRC_ALPHA,SUPER_VERTEX_COLOR_MODE_ADD) );

  color = 0x00404040;
  SuperGraphics_fill_textured_box( 0, h>>1, w>>1, h>>1, 0, 0, 1, 1, color, img2 );
  SuperGraphics_fill_textured_box( w>>1, 0, w>>1, h>>1, 0, 0, 1, 1, color, img2 );

  SuperGraphics_draw_line( 0, 0, mouse_x, mouse_y, -1 );
  SuperGraphics_draw_line( w, 0, mouse_x, mouse_y, -1 );
  SuperGraphics_draw_line( 0, h, mouse_x, mouse_y, -1 );
  SuperGraphics_draw_line( w, h, mouse_x, mouse_y, -1 );

}

void event_handler( SuperEvent* event )
{
  switch (event->type)
  {
    case SUPER_EVENT_DRAW:
      draw();
      break;

    case SUPER_EVENT_POINTER_PRESS:
      printf( "Mouse press #%d at %lf, %lf\n", event->pointer_press.button_index, event->pointer_press.x, event->pointer_press.y );
      mouse_x = event->pointer_press.x;
      mouse_y = event->pointer_press.y;
      break;

    case SUPER_EVENT_POINTER_MOVEMENT:
      printf( "Mouse moved to %lf, %lf\n", event->pointer_movement.x, event->pointer_movement.y );
      mouse_x = event->pointer_press.x;
      mouse_y = event->pointer_press.y;
      break;

    case SUPER_EVENT_POINTER_RELEASE:
      printf( "Mouse release #%d at %lf, %lf\n", event->pointer_release.button_index, event->pointer_release.x, event->pointer_release.y );
      mouse_x = event->pointer_press.x;
      mouse_y = event->pointer_press.y;
      break;

    case SUPER_EVENT_SCROLL:
      printf( "Scroll %lf, %lf\n", event->scroll.dx, event->scroll.dy );
      break;

    case SUPER_EVENT_KEY_PRESS:
      if (event->key_press.is_repeat)
      {
        printf( "Key   press: %c (%d,%d) (repeat)\n", event->key_press.unicode, event->key_press.keycode, event->key_press.syscode );
      }
      else
      {
        printf( "Key   press: %c (%d,%d)\n", event->key_press.unicode, event->key_press.keycode, event->key_press.syscode );
      }
      break;

    case SUPER_EVENT_KEY_RELEASE:
      printf( "Key release: %c (%d,%d)\n", event->key_press.unicode, event->key_press.keycode, event->key_press.syscode );
      break;

    case SUPER_EVENT_POINTER_FOCUS:
      printf( "Pointer focus %s\n", event->pointer_focus.focus_gained ? "gained" : "lost" );
      break;
  }
}

@implementation SuperAppAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    // Insert code here to initialize your application
  SuperEventSystem_configure( event_handler, 0 );
}

@end
