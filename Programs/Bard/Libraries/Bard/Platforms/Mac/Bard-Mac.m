#import <Cocoa/Cocoa.h>

#include "Bard-Mac.h"

#define BARD_UI_WINDOW_STYLE_CLOSABLE    1
#define BARD_UI_WINDOW_STYLE_RESIZABLE   2
#define BARD_UI_WINDOW_STYLE_BORDERLESS  4
#define BARD_UI_WINDOW_STYLE_FULLSCREEN  8

//=============================================================================
// PROTOTYPES
//=============================================================================
void BardUIFrame__native_create__String_Integer_Integer_Integer_Integer_Integer( BardVM* vm );

void BardUIGraphics__draw_box__Real_Real_Real_Real_Integer( BardVM* vm );
void BardUIGraphics__draw_line__Real_Real_Real_Real_Integer( BardVM* vm );
void BardUIGraphics__fill_box__Real_Real_Real_Real_Integer( BardVM* vm );
void BardUIGraphics__native_clear_clip( BardVM* vm );
void BardUIGraphics__native_draw_text__String_Integer_Integer_Real_Real_Integer_Integer( BardVM* vm );
void BardUIGraphics__native_draw_text__CharacterList_Integer_Integer_Real_Real_Integer_Integer( BardVM* vm );
void BardUIGraphics__native_measure_text__String_Integer_Integer( BardVM* vm );
void BardUIGraphics__native_measure_text__CharacterList_Integer_Integer( BardVM* vm );
void BardUIGraphics__native_set_clip__Real_Real_Real_Real( BardVM* vm );

/*
void BardUIDisplay__size( BardVM* vm );

void BardUIWindow__native_create__Box_String_Integer( BardVM* vm );
void BardUIWindow__native_set_position__Integer_Box( BardVM* vm );
*/

void BardUIManager__native_exit__Integer( BardVM* vm );

void BardUINativeComponent__native_close__Integer( BardVM* vm );
void BardUINativeComponent__native_set_visible__Integer_Logical( BardVM* vm );


//=============================================================================
// GLOBALS
//=============================================================================
BardVM*          Bard_vm = NULL;
BardResourceBank BardUI_components;
int              BardUI_graphics_height;
static int BardUIGraphics_saved_graphics_state_count = 0;

BardVM* Bard_init()
{
  Bard_vm = BardVM_create();

  BardStandardLibrary_configure( Bard_vm );
  BardUILibrary_configure( Bard_vm );

  return Bard_vm;
}

bool Bard_load( NSString* program_name )
{
  BardVM* vm = Bard_vm;
  if (BardVM_load_bc_file(vm,[[[NSBundle mainBundle] pathForResource:program_name ofType:@"bc"] UTF8String]))
  {
    BardVM_create_main_object( vm );
    return true;
  }
  else
  {
    printf( "Error loading file." );
    BardVM_destroy( vm );
    Bard_vm = NULL;
    return false;
  }
}


NSString* BardString_to_ns_string( BardObject* obj )
{
  if ( !obj ) return nil;

  BardString* string_obj = (BardString*) obj;
  return [NSString stringWithCharacters:(const unichar*)(string_obj->characters) length:string_obj->count];
}

@interface BardWindow : NSWindow
@end

@implementation BardWindow
- (BOOL) windowShouldClose:(id)sender
{
  int window_id = BardResourceBank_find( &BardUI_components, (__bridge void*) sender );
  BardVM_queue_message( Bard_vm, "UIComponent.close" );
  BardVM_queue_message_arg( Bard_vm, "id", "%d", window_id );
  BardVM_dispatch_messages( Bard_vm );

  return NO;
}
@end


@interface BardView : NSView
@end

@implementation BardView

- (void)drawRect:(NSRect)dirtyRect
{
  [super drawRect:dirtyRect];

  BardUI_graphics_height = (int) self.bounds.size.height;

  [[NSGraphicsContext currentContext] setShouldAntialias:NO];

  BardType*   type_UIManager = BardVM_find_type( Bard_vm, "UIManager" );
  BardMethod* m_draw         = BardType_find_method( type_UIManager, "draw(Integer,Integer,Integer)" );
  if (type_UIManager && m_draw)
  {
    // Let Bard control the drawing
    BardVM_push_object( Bard_vm, type_UIManager->singleton );

    int window_id = BardResourceBank_find( &BardUI_components, (__bridge void*) [self window] );
    BardVM_push_integer( Bard_vm, window_id );

    BardVM_push_integer( Bard_vm, self.bounds.size.width );
    BardVM_push_integer( Bard_vm, self.bounds.size.height );
    BardVM_invoke( Bard_vm, type_UIManager, m_draw );
  }

  while (BardUIGraphics_saved_graphics_state_count) 
  {
    [NSGraphicsContext restoreGraphicsState];
    --BardUIGraphics_saved_graphics_state_count;
  }
}

@end


//=============================================================================
// configure
//=============================================================================
void BardUILibrary_configure( BardVM* vm )
{
  BardResourceBank_init( &BardUI_components );

  /*
  BardVM_register_native_method( vm, "UIFrame", "native_create(String,Integer,Integer,Integer,Integer,Integer)",
      BardUIFrame__native_create__String_Integer_Integer_Integer_Integer_Integer );

  BardVM_register_native_method( vm, "UIGraphics", "draw_box(Real,Real,Real,Real,Integer)", 
      BardUIGraphics__draw_box__Real_Real_Real_Real_Integer );
  BardVM_register_native_method( vm, "UIGraphics", "draw_line(Real,Real,Real,Real,Integer)", 
      BardUIGraphics__draw_line__Real_Real_Real_Real_Integer );
  BardVM_register_native_method( vm, "UIGraphics", "fill_box(Real,Real,Real,Real,Integer)", 
      BardUIGraphics__fill_box__Real_Real_Real_Real_Integer );
  BardVM_register_native_method( vm, "UIGraphics", "native_clear_clip()", BardUIGraphics__native_clear_clip );
  BardVM_register_native_method( vm, "UIGraphics", "native_draw_text(String,Integer,Integer,Real,Real,Integer,Integer)", 
      BardUIGraphics__native_draw_text__String_Integer_Integer_Real_Real_Integer_Integer );
  BardVM_register_native_method( vm, "UIGraphics", "native_draw_text(Character[],Integer,Integer,Real,Real,Integer,Integer)", 
      BardUIGraphics__native_draw_text__CharacterList_Integer_Integer_Real_Real_Integer_Integer );
  BardVM_register_native_method( vm, "UIGraphics", "native_measure_text(String,Integer,Integer)", 
      BardUIGraphics__native_measure_text__String_Integer_Integer );
  BardVM_register_native_method( vm, "UIGraphics", "native_measure_text(Character[],Integer,Integer)", 
      BardUIGraphics__native_measure_text__CharacterList_Integer_Integer );
  BardVM_register_native_method( vm, "UIGraphics", "native_set_clip(Real,Real,Real,Real)",
      BardUIGraphics__native_set_clip__Real_Real_Real_Real );

  BardVM_register_native_method( vm, "UIManager", "native_exit(Integer)", BardUIManager__native_exit__Integer );

  BardVM_register_native_method( vm, "UINativeComponent", "native_close(Integer)", BardUINativeComponent__native_close__Integer );
  BardVM_register_native_method( vm, "UINativeComponent", "native_set_visible(Integer,Logical)", 
      BardUINativeComponent__native_set_visible__Integer_Logical );
  */
}

#ifdef NOCOMPILE
//=============================================================================
//  UIWindow
//=============================================================================
void BardUIFrame__native_create__String_Integer_Integer_Integer_Integer_Integer( BardVM* vm )
{
  BardInteger h = BardVM_pop_integer( vm );
  BardInteger w = BardVM_pop_integer( vm );
  BardInteger y = BardVM_pop_integer( vm );
  BardInteger x = BardVM_pop_integer( vm );
  int     flags = BardVM_pop_integer( vm );
  BardObject* title_obj = BardVM_pop_object( vm );

  BardVM_pop_discard( vm ); // BardUIFrame object

  int style = 0;

  if (title_obj)
  {
    style |= NSTitledWindowMask;
  }

  int screen_width  = [[NSScreen mainScreen] frame].size.width;
  int screen_height = [[NSScreen mainScreen] frame].size.height;

  //if (flags & BARD_UI_WINDOW_STYLE_CLOSABLE)   style |= NSClosableWindowMask;
  //if (flags & BARD_UI_WINDOW_STYLE_RESIZABLE)  style |= NSResizableWindowMask;
  style |= NSClosableWindowMask;
  style |= NSResizableWindowMask;

  if (flags & BARD_UI_WINDOW_STYLE_FULLSCREEN)
  {
    //style |= NSFullScreenWindowMask;
    x = 0;
    y = 0;
    w = screen_width;
    h = screen_height;
  }
  else
  {
    screen_height -= [[[NSApplication sharedApplication] mainMenu] menuBarHeight];
  }

  if (flags & BARD_UI_WINDOW_STYLE_BORDERLESS) style = 0;

  BardWindow* window = [[BardWindow alloc] initWithContentRect:NSMakeRect(0,0,w,h)
    styleMask:style backing:NSBackingStoreBuffered defer:NO];

  BardView* view = [[BardView alloc] init];
  [window setContentView:view];

  if (flags & BARD_UI_WINDOW_STYLE_FULLSCREEN)
  {
    [window setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];
  }

  if (title_obj)
  {
    [window setTitle:BardString_to_ns_string(title_obj)];
  }

  [window setFrameTopLeftPoint:NSMakePoint(x,screen_height-y)];

  [window toggleFullScreen:nil];

  int window_id = BardResourceBank_add( &BardUI_components, (void *)CFBridgingRetain(window) );
  BardVM_push_integer( vm, window_id );
}

/*
void BardUIWindow__native_set_position__Integer_Box( BardVM* vm )
{
  BardReal x      = BardVM_pop_real( vm );
  BardReal y      = BardVM_pop_real( vm );
  BardReal width  = BardVM_pop_real( vm );
  BardReal height = BardVM_pop_real( vm );
  int window_id   = BardVM_pop_integer( vm );
  BardVM_pop_discard( vm );  // context

  BardWindow* window = (__bridge BardWindow *)(BardResourceBank_get( &BardUI_components, window_id ));
  if (window)
  {
    int screen_height = [[NSScreen mainScreen] frame].size.height;
    NSRect frame_rect = [window frameRectForContentRect:NSMakeRect(x,screen_height - y,width,height)];
    [window setFrame:frame_rect display:YES];
  }
}
*/

//=============================================================================
//  BardUIGraphics
//=============================================================================
void BardUIGraphics__draw_box__Real_Real_Real_Real_Integer( BardVM* vm )
{
  int color = BardVM_pop_integer( vm );
  double h  = floor( BardVM_pop_real( vm ) );
  double w  = floor( BardVM_pop_real( vm ) );
  double y  = floor( BardVM_pop_real( vm ) );
  double x  = floor( BardVM_pop_real( vm ) );
  BardVM_pop_discard( vm );

  y = (BardUI_graphics_height - y) - h;

  float a = ((color >> 24) & 255) / 255.0f;
  float r = ((color >> 16) & 255) / 255.0f;
  float g = ((color >> 8) & 255) / 255.0f;
  float b = (color & 255) / 255.0f;

  [[NSColor colorWithRed:r green:g blue:b alpha:a] setFill];
  NSFrameRect( NSMakeRect(x,y,w,h) );
}

void BardUIGraphics__draw_line__Real_Real_Real_Real_Integer( BardVM* vm )
{
  int color = BardVM_pop_integer( vm );
  double y2  = floor( BardVM_pop_real( vm ) );
  double x2  = floor( BardVM_pop_real( vm ) );
  double y1  = floor( BardVM_pop_real( vm ) );
  double x1  = floor( BardVM_pop_real( vm ) );
  BardVM_pop_discard( vm );

  y2 = (BardUI_graphics_height - y2) - 1;
  y1 = (BardUI_graphics_height - y1) - 1;

  float a = ((color >> 24) & 255) / 255.0f;
  float r = ((color >> 16) & 255) / 255.0f;
  float g = ((color >> 8) & 255) / 255.0f;
  float b = (color & 255) / 255.0f;

  [[NSColor colorWithRed:r green:g blue:b alpha:a] set];

  //NSBezierPath* path = [NSBezierPath bezierPath];
  //[path moveToPoint:NSMakePoint(x1,y1)];
  //[path lineToPoint:NSMakePoint(x2,y2)];
  //[path 
  [NSBezierPath strokeLineFromPoint:NSMakePoint(x1,y1) toPoint:NSMakePoint(x2,y2)];
}

void BardUIGraphics__fill_box__Real_Real_Real_Real_Integer( BardVM* vm )
{
  int color = BardVM_pop_integer( vm );
  double h  = floor( BardVM_pop_real( vm ) );
  double w  = floor( BardVM_pop_real( vm ) );
  double y  = floor( BardVM_pop_real( vm ) );
  double x  = floor( BardVM_pop_real( vm ) );
  BardVM_pop_discard( vm );

  y = (BardUI_graphics_height - y) - h;

  float a = ((color >> 24) & 255) / 255.0f;
  float r = ((color >> 16) & 255) / 255.0f;
  float g = ((color >> 8) & 255) / 255.0f;
  float b = (color & 255) / 255.0f;

  [[NSColor colorWithRed:r green:g blue:b alpha:a] set];
  NSRectFill( NSMakeRect(x,y,w,h) );
}

void BardUIGraphics__native_clear_clip( BardVM* vm )
{
  BardVM_pop_discard( vm );

  while (BardUIGraphics_saved_graphics_state_count)
  {
    [NSGraphicsContext restoreGraphicsState];
    --BardUIGraphics_saved_graphics_state_count;
  }
}

void BardUIGraphics_draw_text( BardCharacter* text, int count, double x, double y, int fg_color, int bg_color )
{
  float fg_a = 255.0;
  float fg_r = ((fg_color >> 16) & 255) / 255.0f;
  float fg_g = ((fg_color >> 8) & 255) / 255.0f;
  float fg_b = (fg_color & 255) / 255.0f;

  //float bg_r = (bg_color >> 16) & 255;
  //float bg_g = (bg_color >> 8) & 255;
  //float bg_b = bg_color & 255;

  //float components[4];
  //components[0] = fg_r;
  //components[1] = fg_g;
  //components[2] = fg_b;
  //components[3] = fg_a;
  //CGContextSetFillColorWithColor( UIGraphicsGetCurrentContext(), components );
  //NSDictionary* attributes = [NSDictionary dictionaryWithObjectsAndKeys:
      //[[NSColor colorWithRed:1.0f green:0.0f blue:1.0f alpha:1.0f] CGColor], 
      //(__bridge id)(kCTForegroundColorAttributeName),
      //nil];
                            //(__bridge id)(font), kCTFontAttributeName,
                            //[[NSColor whiteColor] CGColor], (__bridge id)(kCTForegroundColorAttributeName),
                            //nil];

  NSString* st = [NSString stringWithCharacters:(const unichar*)text length:count];

  NSDictionary* attributes = [NSDictionary dictionaryWithObjectsAndKeys:
    [NSColor colorWithRed:1.0f green:1.0f blue:1.0f alpha:1.0f], NSForegroundColorAttributeName,
    //[NSFont systemFontOfSize:24], NSFontAttributeName,
    nil];

  [st drawAtPoint:NSMakePoint((float)x,(float)y) withAttributes:attributes];
}

void BardUIGraphics__native_draw_text__String_Integer_Integer_Real_Real_Integer_Integer( BardVM* vm )
{
  BardInteger bg_color = BardVM_pop_integer( vm );
  BardInteger fg_color = BardVM_pop_integer( vm );
  BardReal y = BardVM_pop_real( vm );
  BardReal x = BardVM_pop_real( vm );
  int i2 = BardVM_pop_integer( vm );
  int i1 = BardVM_pop_integer( vm );
  BardString* text_obj = BardVM_pop_string( vm );

  BardVM_pop_discard( vm ); // UIGraphics context

  y = (BardUI_graphics_height - y);  // - 1 ?

  BardUIGraphics_draw_text( text_obj->characters+i1, (i2-i1)+1, x, y, fg_color, bg_color );
}

void BardUIGraphics__native_draw_text__CharacterList_Integer_Integer_Real_Real_Integer_Integer( BardVM* vm )
{
  BardInteger bg_color = BardVM_pop_integer( vm );
  BardInteger fg_color = BardVM_pop_integer( vm );
  BardReal y = BardVM_pop_real( vm );
  BardReal x = BardVM_pop_real( vm );
  int i2 = BardVM_pop_integer( vm );
  int i1 = BardVM_pop_integer( vm );
  BardObject* list = BardVM_pop_object( vm );
  BardCharacter*  data = BardList_data( list )->character_data;

  BardVM_pop_discard( vm ); // UIGraphics context

  y = (BardUI_graphics_height - y);  // - 1 ?

  BardUIGraphics_draw_text( data+i1, (i2-i1)+1, x, y, fg_color, bg_color );
}


void BardUIGraphics_native_measure_text( BardVM* vm, BardCharacter* data, int count )
{
  // Pushes height and then width of text
  double w = 0.0;
  double h = 0.0;

  if (count)
  {
    NSString* st = [NSString stringWithCharacters:(const unichar*)data length:count];
    NSRect r = [st boundingRectWithSize:NSMakeSize(0,0) options:0 attributes:nil];
    w = r.size.width;
    h = r.size.height;
  }

  BardVM_push_real( vm, h );
  BardVM_push_real( vm, w );
}

void BardUIGraphics__native_measure_text__String_Integer_Integer( BardVM* vm )
{
  int i2 = BardVM_pop_integer( vm );
  int i1 = BardVM_pop_integer( vm );
  BardString* text_obj = BardVM_pop_string( vm );
  BardVM_pop_discard( vm ); // UIGraphics context

  BardUIGraphics_native_measure_text( vm, text_obj->characters+i1, (i2-i1)+1 );
}

void BardUIGraphics__native_measure_text__CharacterList_Integer_Integer( BardVM* vm )
{
  int w, h;

  int i2 = BardVM_pop_integer( vm );
  int i1 = BardVM_pop_integer( vm );
  BardObject* list = BardVM_pop_object( vm );
  BardCharacter*  data = BardList_data( list )->character_data;
  BardVM_pop_discard( vm ); // UIGraphics context

  BardUIGraphics_native_measure_text( vm, data+i1, (i2-i1)+1 );
}

void BardUIGraphics__native_set_clip__Real_Real_Real_Real( BardVM* vm )
{
  double h = BardVM_pop_real( vm );
  double w = BardVM_pop_real( vm );
  double y = BardVM_pop_real( vm );
  double x = BardVM_pop_real( vm );
  BardVM_pop_discard( vm );

  y = (BardUI_graphics_height - y) - h;

  while (BardUIGraphics_saved_graphics_state_count)
  {
    [NSGraphicsContext restoreGraphicsState];
    --BardUIGraphics_saved_graphics_state_count;
  }

  ++BardUIGraphics_saved_graphics_state_count;
  [NSGraphicsContext saveGraphicsState];

  NSRectClip( NSMakeRect(x,y,w,h) );
}

//=============================================================================
//  BardUIManager
//=============================================================================
void BardUIManager__native_exit__Integer( BardVM* vm )
{
  BardVM_pop_discard( vm ); // error code
  BardVM_pop_discard( vm );
  [[NSApplication sharedApplication] terminate: nil];
}

//=============================================================================
//  BardUINativeComponent
//=============================================================================
void BardUINativeComponent__native_close__Integer( BardVM* vm )
{
  int window_id = BardVM_pop_integer( vm );
  BardVM_pop_discard( vm );  // context

  BardWindow* window = (__bridge BardWindow*)(BardResourceBank_get( &BardUI_components, window_id ));
  if (window)
  {
    [window close];
  }
}

void BardUINativeComponent__native_set_visible__Integer_Logical( BardVM* vm )
{
  int setting   = BardVM_pop_integer( vm );
  int window_id = BardVM_pop_integer( vm );
  BardVM_pop_discard( vm );  // context

  BardWindow* window = (__bridge BardWindow *)(BardResourceBank_get( &BardUI_components, window_id ));
  if (window)
  {
    if (setting)
    {
      [window makeKeyAndOrderFront:nil];
    }
  }
}

#endif
