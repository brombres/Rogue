#import <AppKit/NSOpenGLView.h>
#import <Cocoa/Cocoa.h>
#import <CoreVideo/CoreVideo.h>
#import <OpenGL/gl.h>

#import "SuperUI-Mac.h"
#import "SuperGraphics.h"

#include <stdlib.h>
#include <string.h>

#define BRIDGE_RETAIN( objc_ptr ) ((void*)CFBridgingRetain(objc_ptr))
#define BRIDGE_CAST( ptr, type )  ((__bridge type) ptr)

//=============================================================================
// SuperUIWindowInfo
//=============================================================================
SuperUIWindowInfo* SuperUIWindowInfo_create( void* window, void* user_data )
{
  SuperUIWindowInfo* w = Super_allocate( SuperUIWindowInfo );
  w->window = window;
  w->user_data = user_data;
  return w;
}


//=============================================================================
// SuperUI
//=============================================================================

//-----------------------------------------------------------------------------
// GLOBALS
//-----------------------------------------------------------------------------
int                SuperUI_configured = 0;
SuperResourceBank* SuperUI_window_bank = NULL;
NSOpenGLContext*   SuperUI_shared_gl_context = nil;
CVDisplayLinkRef   SuperUI_display_link;


//=============================================================================
// SuperNSWindow
//=============================================================================
@interface SuperNSWindow : NSWindow
@end

@implementation SuperNSWindow
- (BOOL) windowShouldClose:(id)sender
{
  //int window_id = BardResourceBank_find( &BardUI_components, (__bridge void*) sender );
  //BardVM_queue_message( Bard_vm, "UIComponent.close" );
  //BardVM_queue_message_arg( Bard_vm, "id", "%d", window_id );
  //BardVM_dispatch_messages( Bard_vm );

  return NO;
}
@end

//=============================================================================
// SuperNSView
//=============================================================================
static CVReturn Super_display_link_callback( CVDisplayLinkRef display_link, const CVTimeStamp* now, const CVTimeStamp* output_time,
    CVOptionFlags input_flags, CVOptionFlags* output_flags, void* context );

@interface SuperNSView : NSOpenGLView
{
}
@end


void SuperGL_clear(int);

@implementation SuperNSView

- (void)dealloc
{
}


- (void)drawRect:(NSRect)bounds
{
  int window_id = SuperResourceBank_find_id( SuperUI_window_bank, BRIDGE_CAST([self window],void*) );
  SuperUIWindowInfo* window_info = (SuperUIWindowInfo*) SuperResourceBank_get_info( SuperUI_window_bank, window_id );

  window_info->width = bounds.size.width;
  window_info->height = bounds.size.height;
  window_info->clip_x = bounds.origin.x;
  window_info->clip_y = bounds.origin.y;
  window_info->clip_width = bounds.size.width;
  window_info->clip_height = bounds.size.height;

  SuperGraphics_draw_window( window_info );

  /*
  if (SuperUI_draw_callback)
  {
    SuperGraphics_begin_draw();

    int window_id = SuperResourceBank_find_id( SuperUI_window_bank, BRIDGE_CAST([self window],void*) );
    void* user_data = SuperUI_get_window_user_data( window_id );
    int display_w = self.bounds.size.width;
    int display_h = self.bounds.size.height;
    int clip_x = bounds.origin.x;
    int clip_y = bounds.origin.y;
    int clip_w = bounds.size.width;
    int clip_h = bounds.size.height;

    SuperUI_draw_callback( window_id, user_data, display_w, display_h, clip_x, clip_y, clip_w, clip_h );

    SuperGraphics_end_draw();
  }

  //BardUI_graphics_height = (int) self.bounds.size.height;

  //[[NSGraphicsContext currentContext] setShouldAntialias:NO];

  //BardType*   type_UIManager = BardVM_find_type( Bard_vm, "UIManager" );
  //BardMethod* m_draw         = BardType_find_method( type_UIManager, "draw(Integer,Integer,Integer)" );
  //if (type_UIManager && m_draw)
  //{
  //  // Let Bard control the drawing
  //  BardVM_push_object( Bard_vm, type_UIManager->singleton );

  //  int window_id = BardResourceBank_find( &BardUI_components, (__bridge void*) [self window] );
  //  BardVM_push_integer( Bard_vm, window_id );

  //  BardVM_push_integer( Bard_vm, self.bounds.size.width );
  //  BardVM_push_integer( Bard_vm, self.bounds.size.height );
  //  BardVM_invoke( Bard_vm, type_UIManager, m_draw );
  //}

  //while (BardUIGraphics_saved_graphics_state_count) 
  //{
  //  [NSGraphicsContext restoreGraphicsState];
  //  --BardUIGraphics_saved_graphics_state_count;
  //}
  */
}

//- (CVReturn)getFrameForTime:(const CVTimeStamp*)output_time

- (void) updateAndDraw
{
  int window_id = SuperResourceBank_find_id( SuperUI_window_bank, BRIDGE_CAST([self window],void*) );
  SuperUIWindowInfo* window_info = (SuperUIWindowInfo*) SuperResourceBank_get_info( SuperUI_window_bank, window_id );

  NSRect bounds = self.bounds;
  window_info->width = bounds.size.width;
  window_info->height = bounds.size.height;
  window_info->clip_x = bounds.origin.x;
  window_info->clip_y = bounds.origin.y;
  window_info->clip_width = bounds.size.width;
  window_info->clip_height = bounds.size.height;

  SuperGraphics_draw_window( window_info );

}


- (void)prepareOpenGL
{
  // Synchronize buffer swaps with vertical refresh rate
  GLint swapInt = 1;
  [[self openGLContext] setValues:&swapInt forParameter:NSOpenGLCPSwapInterval];

}

@end

static CVReturn Super_display_link_callback( CVDisplayLinkRef display_link, const CVTimeStamp* now, const CVTimeStamp* output_time,
    CVOptionFlags input_flags, CVOptionFlags* output_flags, void* context )
{
  //CVReturn result = [BRIDGE_CAST(context,SuperNSView*) getFrameForTime:output_time];
  //return result;
  //[BRIDGE_CAST(context,SuperNSView*) performSelectorOnMainThread:@selector(updateAndDraw) withObject:nil waitUntilDone:YES];
  SuperUI_draw_all_windows();
  return kCVReturnSuccess;
}



//=============================================================================
// PUBLIC METHODS
//=============================================================================
void SuperUI_configure()
{
  if (SuperUI_configured) return;
  SuperUI_configured = 1;

  SuperUI_window_bank = SuperResourceBank_create();

  // Create the the OpenGL context that will be shared between all windows.
  SuperUI_shared_gl_context = [[NSOpenGLContext alloc] initWithFormat:[NSOpenGLView defaultPixelFormat] shareContext:SuperUI_shared_gl_context];

  // Set up a single display link that will draw all windows
  CVDisplayLinkCreateWithActiveCGDisplays( &SuperUI_display_link );

  // Set the renderer output callback function
  CVDisplayLinkSetOutputCallback( SuperUI_display_link, &Super_display_link_callback, NULL );

  // Set the display link for the current renderer
  CGLContextObj cglContext = [SuperUI_shared_gl_context CGLContextObj];
  CGLPixelFormatObj cglPixelFormat = [[NSOpenGLView defaultPixelFormat] CGLPixelFormatObj];
  CVDisplayLinkSetCurrentCGDisplayFromOpenGLContext(SuperUI_display_link, cglContext, cglPixelFormat);

  // Activate the display link
  CVDisplayLinkStart(SuperUI_display_link);
}

void SuperUI_retire()
{
  CVDisplayLinkRelease( SuperUI_display_link );
  SuperUI_window_bank = SuperResourceBank_destroy( SuperUI_window_bank );
}


int SuperUI_create_window( const char* title, int x, int y, int w, int h, int flags, void* user_data  )
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

void  SuperUI_draw_all_windows()
{
  int i;
  SuperIntList* window_ids = SuperUI_window_bank->active_ids;
  for (i=0; i<window_ids->count; ++i)
  {
    SuperUI_draw_window( window_ids->data[i] );
  }
}

void  SuperUI_draw_window( int window_id )
{
  NSWindow* window = BRIDGE_CAST( SuperResourceBank_get_resource(SuperUI_window_bank,window_id), NSWindow* );
  if (window)
  {
    [window display];
  }
}

void SuperUI_get_window_content_size( int window_id, int* width, int *height )
{
  NSWindow* window = BRIDGE_CAST( SuperResourceBank_get_resource(SuperUI_window_bank,window_id), NSWindow* );
  if (window)
  {
    NSRect r = [window contentRectForFrameRect:window.frame];
    *width  = r.size.width;
    *height = r.size.height;
  }
  else
  {
    *width = 0;
    *height = 0;
  }
}

void* SuperUI_get_window_user_data( int window_id )
{
  SuperUIWindowInfo* info = (SuperUIWindowInfo*) SuperResourceBank_get_info( SuperUI_window_bank, window_id );
  if (info) return info->user_data;
  return NULL;
}

void SuperUI_show_window( int window_id, int visible )
{
  NSWindow* window = BRIDGE_CAST( SuperResourceBank_get_resource(SuperUI_window_bank,window_id), NSWindow* );
  [window makeKeyAndOrderFront:nil];
}


//=============================================================================
// SuperGraphics
//=============================================================================

void SuperGraphics_clip( int x, int y, int w, int h )
{
}

void SuperGraphics_destroy_texture( int texture_id )
{
}

void SuperGraphics_draw_window( struct SuperUIWindowInfo* info )
{
  if ( !info ) return;

  SuperEvent event;
  event.type = SUPER_EVENT_DRAW;
  event.draw.window_id = info->window_id;
  event.draw.display_width = info->width;
  event.draw.display_height = info->height;
  event.draw.clip_x = info->clip_x;
  event.draw.clip_y = info->clip_y;
  event.draw.clip_width = info->clip_width;
  event.draw.clip_height = info->clip_height;

  SuperEventSystem_dispatch( &event ); // drawing happens here
  SuperGraphics_flush();

  glFlush();
}

