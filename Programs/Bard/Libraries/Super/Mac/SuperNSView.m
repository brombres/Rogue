#import  "SuperNSView.h"
#include "SuperGraphics.h"

#define BRIDGE_RETAIN( objc_ptr ) ((void*)CFBridgingRetain(objc_ptr))
#define BRIDGE_CAST( ptr, type )  ((__bridge type) ptr)

SuperNSView* view_1 = nil;
SuperNSView* view_2 = nil;

static int Super_syscode_to_keycode_map[128] =
{
  SUPER_KEYCODE_A,               //   0 -> a
  SUPER_KEYCODE_S,               //   1 -> s
  SUPER_KEYCODE_D,               //   2 -> d
  SUPER_KEYCODE_F,               //   3 -> f
  SUPER_KEYCODE_H,               //   4 -> h
  SUPER_KEYCODE_G,               //   5 -> g
  SUPER_KEYCODE_Z,               //   6 -> z
  SUPER_KEYCODE_X,               //   7 -> x
  SUPER_KEYCODE_C,               //   8 -> c
  SUPER_KEYCODE_V,               //   9 -> v
  0,                             //  10 -> (unknown)
  SUPER_KEYCODE_B,               //  11 -> b
  SUPER_KEYCODE_Q,               //  12 -> q
  SUPER_KEYCODE_W,               //  13 -> w
  SUPER_KEYCODE_E,               //  14 -> e
  SUPER_KEYCODE_R,               //  15 -> r
  SUPER_KEYCODE_Y,               //  16 -> y
  SUPER_KEYCODE_T,               //  17 -> t
  SUPER_KEYCODE_1,               //  18 -> 1
  SUPER_KEYCODE_2,               //  19 -> 2
  SUPER_KEYCODE_3,               //  20 -> 3
  SUPER_KEYCODE_4,               //  21 -> 4
  SUPER_KEYCODE_6,               //  22 -> 6
  SUPER_KEYCODE_5,               //  23 -> 5
  SUPER_KEYCODE_EQUALS,          //  24 -> EQUALS
  SUPER_KEYCODE_9,               //  25 -> 9
  SUPER_KEYCODE_7,               //  26 -> 7
  SUPER_KEYCODE_MINUS,           //  27 -> MINUS
  SUPER_KEYCODE_8,               //  28 -> 8
  SUPER_KEYCODE_0,               //  29 -> 0
  SUPER_KEYCODE_CLOSE_BRACKET,   //  30 -> ]
  SUPER_KEYCODE_O,               //  31 -> o
  SUPER_KEYCODE_U,               //  32 -> u
  SUPER_KEYCODE_OPEN_BRACKET,    //  33 -> [
  SUPER_KEYCODE_I,               //  34 -> i
  SUPER_KEYCODE_P,               //  35 -> p
  SUPER_KEYCODE_ENTER,           //  36 -> ENTER
  SUPER_KEYCODE_L,               //  37 -> l
  SUPER_KEYCODE_J,               //  38 -> j
  SUPER_KEYCODE_APOSTROPHE,      //  39 -> '
  SUPER_KEYCODE_K,               //  40 -> k
  SUPER_KEYCODE_SEMICOLON,       //  41 -> ;
  SUPER_KEYCODE_BACKSLASH,       //  42 -> '\'
  SUPER_KEYCODE_COMMA,           //  43 -> ,
  SUPER_KEYCODE_SLASH,           //  44 -> /
  SUPER_KEYCODE_N,               //  45 -> n
  SUPER_KEYCODE_M,               //  46 -> m
  SUPER_KEYCODE_PERIOD,          //  47 -> .
  SUPER_KEYCODE_TAB,             //  48 -> TAB
  SUPER_KEYCODE_SPACE,           //  49 -> SPACE
  SUPER_KEYCODE_BACKQUOTE,       //  50 -> grave (back quote)
  SUPER_KEYCODE_BACKSPACE,       //  51 -> BACKSPACE
  0,                             //  52 -> (unknown)
  SUPER_KEYCODE_ESCAPE,          //  53 -> ESC
  0,                             //  54 -> (unknown)
  0,                             //  55 -> (unknown)
  0,                             //  56 -> (unknown)
  0,                             //  57 -> (unknown)
  0,                             //  58 -> (unknown)
  0,                             //  59 -> (unknown)
  0,                             //  60 -> (unknown)
  0,                             //  61 -> (unknown)
  0,                             //  62 -> (unknown)
  0,                             //  63 -> (unknown)
  SUPER_KEYCODE_F17,             //  64 -> F17
  SUPER_KEYCODE_NUMPAD_PERIOD,   //  65 -> NUMPAD .
  0,                             //  66 -> (unknown)
  SUPER_KEYCODE_NUMPAD_ASTERISK, //  67 -> NUMPAD *
  0,                             //  68 -> (unknown)
  SUPER_KEYCODE_NUMPAD_PLUS,     //  69 -> NUMPAD +
  0,                             //  70 -> (unknown)
  SUPER_KEYCODE_NUMPAD_NUM_LOCK, //  71 -> CLEAR (Mac)
  0,                             //  72 -> (unknown)
  0,                             //  73 -> (unknown)
  0,                             //  74 -> (unknown)
  SUPER_KEYCODE_SLASH,           //  75 -> NUMPAD /
  0,                             //  76 -> (unknown)
  0,                             //  77 -> (unknown)
  SUPER_KEYCODE_NUMPAD_MINUS,    //  78 -> NUMPAD -
  SUPER_KEYCODE_F18,             //  79 -> F18
  SUPER_KEYCODE_F19,             //  80 -> F19
  SUPER_KEYCODE_NUMPAD_EQUALS,   //  81 -> NUMPAD =
  SUPER_KEYCODE_NUMPAD_0,        //  82 -> NUMPAD 0
  SUPER_KEYCODE_NUMPAD_1,        //  83 -> NUMPAD 1
  SUPER_KEYCODE_NUMPAD_2,        //  84 -> NUMPAD 2
  SUPER_KEYCODE_NUMPAD_3,        //  85 -> NUMPAD 3
  SUPER_KEYCODE_NUMPAD_4,        //  86 -> NUMPAD 4
  SUPER_KEYCODE_NUMPAD_5,        //  87 -> NUMPAD 5
  SUPER_KEYCODE_NUMPAD_6,        //  88 -> NUMPAD 6
  SUPER_KEYCODE_NUMPAD_7,        //  89 -> NUMPAD 7
  0,                             //  90 -> (unknown)
  SUPER_KEYCODE_NUMPAD_8,        //  91 -> NUMPAD 8
  SUPER_KEYCODE_NUMPAD_9,        //  92 -> NUMPAD 9
  0,                             //  93 -> (unknown)
  0,                             //  94 -> (unknown)
  0,                             //  95 -> (unknown)
  SUPER_KEYCODE_F5,              //  96 -> F5
  SUPER_KEYCODE_F6,              //  97 -> F6
  SUPER_KEYCODE_F7,              //  98 -> F7
  SUPER_KEYCODE_F3,              //  99 -> F3
  SUPER_KEYCODE_F8,              // 100 -> F8
  SUPER_KEYCODE_F9,              // 101 -> F9        
  0,                             // 102 -> (unknown)
  SUPER_KEYCODE_F11,             // 103 -> F11           
  0,                             // 104 -> (unknown)
  SUPER_KEYCODE_F13,             // 105 -> F13
  SUPER_KEYCODE_F16,             // 106 -> F16
  SUPER_KEYCODE_F14,             // 107 -> F14
  0,                             // 108 -> (unknown)
  SUPER_KEYCODE_F10,             // 109 -> F10
  0,                             // 110 -> (unknown)
  SUPER_KEYCODE_F12,             // 111 -> F12
  0,                             // 112 -> (unknown)
  SUPER_KEYCODE_F15,             // 113 -> F15
  0,                             // 114 -> (unknown)
  SUPER_KEYCODE_HOME,            // 115 -> HOME
  SUPER_KEYCODE_PAGE_UP,         // 116 -> PAGE UP
  SUPER_KEYCODE_DELETE,          // 117 -> DELETE
  SUPER_KEYCODE_F4,              // 118 -> F4
  SUPER_KEYCODE_END,             // 119 -> END
  SUPER_KEYCODE_F2,              // 120 -> F2
  SUPER_KEYCODE_PAGE_DOWN,       // 121 -> PAGE DOWN
  SUPER_KEYCODE_F1,              // 122 -> F1
  SUPER_KEYCODE_LEFT_ARROW,      // 123 -> LEFT ARROW
  SUPER_KEYCODE_RIGHT_ARROW,     // 124 -> RIGHT ARROW
  SUPER_KEYCODE_DOWN_ARROW,      // 125 -> DOWN ARROW
  SUPER_KEYCODE_UP_ARROW,        // 126 -> UP ARROW
  0                              // 127 -> (unknown)
};

static CVReturn SuperNSView_display_link_callback( CVDisplayLinkRef display_link, const CVTimeStamp* now, const CVTimeStamp* output_time,
    CVOptionFlags input_flags, CVOptionFlags* output_flags, void* view_ptr )
{
  static bool locked = false;
  if (locked) return kCVReturnSuccess;
  locked = true;

  SuperNSView* view = BRIDGE_CAST( view_ptr, SuperNSView* );
  [view drawRect:view.bounds];

  locked = false;
  return kCVReturnSuccess;
}



@implementation SuperNSView
+ (NSOpenGLContext*) createSharedOpenGLContext
{
  static NSOpenGLContext* shared_context = nil;
  shared_context = [[NSOpenGLContext alloc] initWithFormat:[NSOpenGLView defaultPixelFormat] shareContext:shared_context];
  return shared_context;
}

- (void)activateDisplayLink
{
  NSLog( @"Activating display link" );
  CVDisplayLinkCreateWithActiveCGDisplays( &display_link );
  CVDisplayLinkSetOutputCallback( display_link, SuperNSView_display_link_callback, BRIDGE_RETAIN(self) );

  // Set the display link for the current renderer
  CGLContextObj cglContext = [self.gl_context CGLContextObj];
  CGLPixelFormatObj cglPixelFormat = [[NSOpenGLView defaultPixelFormat] CGLPixelFormatObj];
  CVDisplayLinkSetCurrentCGDisplayFromOpenGLContext( display_link, cglContext, cglPixelFormat );

  // Activate the display link
  CVDisplayLinkStart( display_link );
}

- (void) drawDisplay:(int)display_id withSize:(NSSize)display_size
{
  if ( !SuperEventSystem_dispatch_draw(display_id, (int) display_size.width, (int) display_size.height) )
  {
    SuperGraphics_clear( SUPER_CLEAR_COLOR, 0xff000000, 0, 0 );
  }
}

- (BOOL)becomeFirstResponder
{
  if ( ![super becomeFirstResponder] ) return NO;
  SuperEventSystem_dispatch_pointer_focus( self.display_id, true );
  return YES;
}

- (NSPoint)convertEventPositionToLocalCoordinates:(NSEvent*)event
{
  NSPoint point = [event locationInWindow];
  return [self convertPoint:point fromView:nil];
}

- (void)drawRect:(NSRect)bounds
{
  if ( !self.gl_context )
  {
    NSLog( @"Creating shared OpenGL context" );
    self.gl_context = [SuperNSView createSharedOpenGLContext];
    [self setOpenGLContext:self.gl_context];
    [self.gl_context setView:self];

    GLint swap_interval = 1;
    [self.gl_context setValues:&swap_interval forParameter:NSOpenGLCPSwapInterval];

    [self activateDisplayLink];

    [self.window setAcceptsMouseMovedEvents:YES];
    [[self window] makeFirstResponder:self];
  }

  CGLLockContext( [self.gl_context CGLContextObj] );
  [self.gl_context makeCurrentContext];

  NSSize display_size = self.bounds.size;

  SuperGraphics_begin_draw( self.display_id, display_size.width, display_size.height );
  [self drawDisplay:self.display_id withSize:display_size];

  SuperGraphics_end_draw();
  [self.gl_context flushBuffer];

  CGLUnlockContext( [self.gl_context CGLContextObj] );
}

- (void) keyDown:(NSEvent*)event
{
  int syscode = [event keyCode] & 0x7f;
  int keycode = Super_syscode_to_keycode_map[ syscode ];
  NSString* characters = [event characters];
  for (int i=0; i<characters.length; ++i)
  {
    int unicode = [characters characterAtIndex:i];
    SuperEventSystem_dispatch_key_press( self.display_id, unicode, keycode, syscode, [event isARepeat] );
  }
}

- (void) keyUp:(NSEvent*)event
{
  int syscode = [event keyCode] & 0x7f;
  int keycode = Super_syscode_to_keycode_map[ syscode ];
  NSString* characters = [event characters];
  for (int i=0; i<characters.length; ++i)
  {
    int unicode = [characters characterAtIndex:i];
    SuperEventSystem_dispatch_key_release( self.display_id, unicode, keycode, syscode );
  }
}

- (void) handleModifiedKey:(int)modified withMask:(int)mask superKeycode:(int)keycode
{
  if ((modified & mask) == mask)
  {
    bool pressed = !(self.key_modifier_flags & mask);
    if (pressed)
    {
      SuperEventSystem_dispatch_key_press( self.display_id, 0, keycode, 0, 0 );
    }
    else
    {
      SuperEventSystem_dispatch_key_release( self.display_id, 0, keycode, 0 );
    }
  }
}

- (void) flagsChanged:(NSEvent*)event
{
  int new_flags = (int) [event modifierFlags];
  int modified = self.key_modifier_flags ^ new_flags;

  // Caps Lock
  [self handleModifiedKey:modified withMask:NSAlphaShiftKeyMask superKeycode:SUPER_KEYCODE_CAPS_LOCK];

  // Shift
  [self handleModifiedKey:modified withMask:(NSShiftKeyMask|0x2) superKeycode:SUPER_KEYCODE_LEFT_SHIFT];  // Left SHIFT
  [self handleModifiedKey:modified withMask:(NSShiftKeyMask|0x4) superKeycode:SUPER_KEYCODE_RIGHT_SHIFT]; // Right SHIFT

  // Control
  [self handleModifiedKey:modified withMask:(NSControlKeyMask|0x1)    superKeycode:SUPER_KEYCODE_LEFT_CONTROL];  // Left CTRL
  [self handleModifiedKey:modified withMask:(NSControlKeyMask|0x2000) superKeycode:SUPER_KEYCODE_RIGHT_CONTROL]; // Right CTRL

  // Alt
  [self handleModifiedKey:modified withMask:(NSAlternateKeyMask|0x20) superKeycode:SUPER_KEYCODE_LEFT_ALT];  // Left ALT
  [self handleModifiedKey:modified withMask:(NSAlternateKeyMask|0x40) superKeycode:SUPER_KEYCODE_RIGHT_ALT]; // Right ALT

  // Command
  [self handleModifiedKey:modified withMask:(NSCommandKeyMask|0x08) superKeycode:SUPER_KEYCODE_LEFT_OS];  // Left COMMAND
  [self handleModifiedKey:modified withMask:(NSCommandKeyMask|0x10) superKeycode:SUPER_KEYCODE_RIGHT_OS]; // Right COMMAND

  // Numpad ENTER
  [self handleModifiedKey:modified withMask:NSNumericPadKeyMask superKeycode:SUPER_KEYCODE_NUMPAD_ENTER];

  self.key_modifier_flags = new_flags;
}

- (void) mouseDown:(NSEvent*)event
{
  NSPoint point = [self convertEventPositionToLocalCoordinates:event];
  SuperEventSystem_dispatch_pointer_press( self.display_id, point.x, self.bounds.size.height - point.y, 0 );
}

- (void) mouseDragged:(NSEvent*)event
{
  NSPoint point = [self convertEventPositionToLocalCoordinates:event];
  SuperEventSystem_dispatch_pointer_movement( self.display_id, point.x, self.bounds.size.height - point.y );
}

- (void) mouseMoved:(NSEvent*)event
{
  NSPoint point = [self convertEventPositionToLocalCoordinates:event];
  SuperEventSystem_dispatch_pointer_movement( self.display_id, point.x, self.bounds.size.height - point.y );
}

- (void) mouseUp:(NSEvent*)event
{
  NSPoint point = [self convertEventPositionToLocalCoordinates:event];
  SuperEventSystem_dispatch_pointer_release( self.display_id, point.x, self.bounds.size.height - point.y, 0 );
}

- (BOOL)resignFirstResponder
{
  if ( ![super resignFirstResponder] ) return NO;
  SuperEventSystem_dispatch_pointer_focus( self.display_id, false );
  return YES;
}

- (void) rightMouseDown:(NSEvent*)event
{
  NSPoint point = [self convertEventPositionToLocalCoordinates:event];
  point = [self convertPoint:point fromView:nil];
  SuperEventSystem_dispatch_pointer_press( self.display_id, point.x, self.bounds.size.height - point.y, 1 );
}

- (void) rightMouseDragged:(NSEvent*)event
{
  NSPoint point = [self convertEventPositionToLocalCoordinates:event];
  SuperEventSystem_dispatch_pointer_movement( self.display_id, point.x, self.bounds.size.height - point.y );
}

- (void) rightMouseUp:(NSEvent*)event
{
  NSPoint point = [self convertEventPositionToLocalCoordinates:event];
  point = [self convertPoint:point fromView:nil];
  SuperEventSystem_dispatch_pointer_release( self.display_id, point.x, self.bounds.size.height - point.y, 1 );
}

- (void) scrollWheel:(NSEvent*)event
{
  double dx = [event deltaX];
  double dy = [event deltaY];

  if (dx >= 0.0001 || dx <= -0.0001 || dy >= 0.0001 || dy <= -0.0001)
  {
    SuperEventSystem_dispatch_scroll( self.display_id, [event deltaX], [event deltaY] );
  }
}
@end

