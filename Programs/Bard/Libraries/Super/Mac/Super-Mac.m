#import  "Super-Mac.h"
#include "Super.h"

#include <string.h>

static char Super_asset_filepath_buffer[512];

char* Super_locate_asset( const char* path, const char* filename )
{
  const char* filepath;
  if (path)
  {
    int len = (int) strlen( path );
    strcpy( Super_asset_filepath_buffer, path );
    if (len && Super_asset_filepath_buffer[len-1] != '/') Super_asset_filepath_buffer[len++] = '/';
    strcpy( Super_asset_filepath_buffer+len, filename );
    filepath = Super_asset_filepath_buffer;
  }
  else
  {
    filepath = filename;
  }

  NSString* resource_path = [[NSBundle mainBundle] pathForResource:[NSString stringWithUTF8String:filepath] ofType:nil];
  if ( !resource_path ) return 0;

  strcpy( Super_asset_filepath_buffer, [resource_path UTF8String] );
  return Super_asset_filepath_buffer;
}

/*
int Super_create_window( const char* title, int x, int y, int w, int h, int flags, void* user_data  )
{
  int style = 0;

  if (title)
  {
    style |= NSTitledWindowMask;
  }

  int screen_width  = [[NSScreen mainScreen] frame].size.width;
  int screen_height = [[NSScreen mainScreen] frame].size.height;

  //if (flags & BARD_UI_WINDOW_STYLE_CLOSABLE)   style |= NSClosableWindowMask;
  //if (flags & BARD_UI_WINDOW_STYLE_RESIZABLE)  style |= NSResizableWindowMask;
  style |= NSClosableWindowMask;
  style |= NSResizableWindowMask;

  //if (flags & BARD_UI_WINDOW_STYLE_FULLSCREEN)
  //{
  //  //style |= NSFullScreenWindowMask;
  //  x = 0;
  //  y = 0;
  //  w = screen_width;
  //  h = screen_height;
  //}
  //else
  {
    screen_height -= [[[NSApplication sharedApplication] mainMenu] menuBarHeight];
  }

  if (x == -1) x = (screen_width - w) / 2;
  if (y == -1) y = (screen_height - h) / 2;

  //if (flags & BARD_UI_WINDOW_STYLE_BORDERLESS) style = 0;
  NSRect content_rect = NSMakeRect( 0, 0, w, h );

  SuperNSWindow* window = [[SuperNSWindow alloc] initWithContentRect:content_rect
    styleMask:style backing:NSBackingStoreBuffered defer:NO];

  SuperNSView* view = [[SuperNSView alloc] initWithFrame:content_rect pixelFormat:[NSOpenGLView defaultPixelFormat]];

  // Create a new NSOpenGLContext that's shared with any other contexts.
  NSOpenGLContext* gl_context = [[NSOpenGLContext alloc] initWithFormat:[NSOpenGLView defaultPixelFormat] shareContext:SuperUI_shared_gl_context];

  if (SuperUI_window_bank->active_ids->count)
  {
    // One window already exists.  Have this window share the existing OpenGL context.
    //
  }

  [window setContentView:view];
  [view setOpenGLContext:gl_context];
  [gl_context setView:view];

  //if (flags & BARD_UI_WINDOW_STYLE_FULLSCREEN)
  //{
  //  [window setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];
  //}

  if (title) [window setTitle:[NSString stringWithUTF8String:title]];

  [window setFrameTopLeftPoint:NSMakePoint(x,screen_height-y)];

  [window toggleFullScreen:nil];

  void* window_ptr = BRIDGE_RETAIN( window );
  SuperUIWindowInfo* info= SuperUIWindowInfo_create( window_ptr, user_data );
  int window_id = SuperResourceBank_add( SuperUI_window_bank, window_ptr, info );
  info->window_id = window_id;
  info->width = w;
  info->height = h;

  return window_id;
}
*/
