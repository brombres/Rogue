//
//  BardGLView.m
//  BardRendererTest
//
//  Created by Michael Dwan on 8/8/13.
//  Copyright (c) 2013 AdColony. All rights reserved.
//

#import "BardGLView.h"
#import "BardAppDelegate.h"
#import "BardObjCInterface.h"
#include "BardGLCore.h"
#include "BardMessageHandler.h"


double angle = 0.0;
double gx = 0.0;

BardGLTexture test_texture = {};

@implementation BardGLView
@synthesize delegate = _delegate;

+ (Class)layerClass
{
  return [CAEAGLLayer class];
}

- (id)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self)
    {
      [self setupLayer];
      [self initGLContext];
      BardGL_init_gl_renderbuffer();
      BardGL_gltm_init();
      [context renderbufferStorage:GL_RENDERBUFFER fromDrawable:eaglLayer];
      BardGL_init_gl_framebuffer();
      BardGL_shader_build(&BardGL_texture_shader);
      BardGL_shader_build(&BardGL_opaque_shader);
      BardGL_init_gl_vertex_and_index_buffers();
      BardGL_use_linear_filter();
      //int id2 = BardGL_load_texture("mario.png");
      int id2 = BardGL_load_texture("Test_1024x128.png");
      int id1 = BardGL_load_texture("tile_floor.png");
      BardGL_use_texture( id2, -1 );
      BardGL_enable_alpha_blending(TRUE);
      BardGL_use_blend_mode(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      [self setupDisplayLink];
    }
    return self;
}


- (void)setupDisplayLink {
  CADisplayLink* displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(render:)];
  [displayLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
}

// - (void)dealloc
// {
//   [context release];
//   context = nil;
//   [super dealloc];
// }

- (void)setupLayer
{
  eaglLayer = (CAEAGLLayer*)self.layer;
  eaglLayer.opaque = TRUE;
  eaglLayer.drawableProperties = @{ kEAGLDrawablePropertyRetainedBacking:@TRUE,
                                    kEAGLDrawablePropertyColorFormat:kEAGLColorFormatRGBA8
                                  };

  // Enable full resolution on Retina devices
  if ([eaglLayer respondsToSelector:@selector(setContentsScale:)])
  {
    [eaglLayer setContentsScale:[[UIScreen mainScreen] scale]];
  }
}

- (bool)initGLContext
{
  context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
  if(!context || ![EAGLContext setCurrentContext:context]) return FALSE;

//glGetRenderbufferParameterivOES(GL_RENDERBUFFER_OES, GL_RENDERBUFFER_WIDTH_OES, &backingWidth);
//    glGetRenderbufferParameterivOES(GL_RENDERBUFFER_OES, GL_RENDERBUFFER_HEIGHT_OES, &backingHeight);

  float scale = [[UIScreen mainScreen] scale];  // necessary to enable Retina
  BardGL_render_context.display_width = (int) (self.frame.size.width * scale);
  BardGL_render_context.display_height = (int) (self.frame.size.height * scale);
  //printf( "SCALE: %f\n", [[UIScreen mainScreen] scale]);

  return TRUE;
}

double test_angle = 0;
double pi = 0;

double previous_time = 0;

- (void)render:(CADisplayLink*)displayLink
{
  double current_time = [[NSDate date] timeIntervalSince1970];
  double elapsed_time = current_time - previous_time;
  previous_time = current_time;

  //printf( "FPS: %lf\n", 1.0 / elapsed_time );

  if (pi == 0) pi = acos(-1);
  //NSDate *methodStart = [NSDate date];

  //NSString* calls = [self.delegate executeJavascript:@"update({x:123});"];

  //if ( ![calls length] ) return;
  BardGL_clear(0xFF005500);
  BardGL_load_ortho_projection( -BardGL_get_display_width()/2, BardGL_get_display_width()/2,
                              -BardGL_get_display_height()/2, BardGL_get_display_height()/2,
                              -5, 5 );

  BardGL_use_texture( 2, -1 );
  BardGL_enable_texture(TRUE);
  BardGL_draw_box(-BardGL_get_display_width()/2, -BardGL_get_display_height()/2,
                BardGL_get_display_width()/2, BardGL_get_display_height()/2,
                0, 0, 1, 1, 0xFFAAAAAA);

  BardGL_use_texture( 1, -1 );
  BardGL_draw_box(-BardGL_get_display_width()/2, -BardGL_get_display_height()/2,
                BardGL_get_display_width()/2, BardGL_get_display_height()/2,
                0, 0, 1, 1, 0xFFAAAAAA);

  BardGL_use_texture( 1, -1 );
  BardGL_enable_texture(TRUE);

  BardGL_draw_box_rotated( 0, 0,
                         128, 1024,
                         0, 0, 1, 1,
                         -1, -pi/2 );
  /*
  BardGL_draw_box_rotated( 0, 0,
                         128, 128,
                         0, 0, 1, 1,
                         -1,
                         (test_angle/180.0)*pi );
                         */
  test_angle += 90.0 / 60.0;

  // BardGL_use_texture( 2, -1 );
  // BardGL_draw_box(-BardGL_get_display_width()/2, -BardGL_get_display_height()/2,
  //               BardGL_get_display_width()/2, BardGL_get_display_height()/2,
  //               0, 0, 1, 1, 0xFFAAAAAA);

  BardGL_enable_texture(FALSE);

  //BardGL_parse_and_send_js_messages([calls UTF8String]);

  BardGL_render();
  [context presentRenderbuffer:GL_RENDERBUFFER];

  //NSDate *methodFinish = [NSDate date];
  //NSTimeInterval executionTime = [methodFinish timeIntervalSinceDate:methodStart];
  //NSLog(@"executionTime = %f", executionTime);
}

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event
{
  [self.delegate executeJavascript:@"touch_begin()"];
  [self.delegate touchesBegan:touches withEvent:event];
}

/*
// Only override drawRect: if you perform custom drawing.
// An empty implementation adversely affects performance during animation.
- (void)drawRect:(CGRect)rect
{
    // Drawing code
}
*/

@end
