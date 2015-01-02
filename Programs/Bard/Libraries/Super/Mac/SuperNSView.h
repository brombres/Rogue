#import <AppKit/NSOpenGLView.h>
#import <Cocoa/Cocoa.h>
#import <CoreVideo/CoreVideo.h>
#import <OpenGL/gl.h>

@interface SuperNSView : NSOpenGLView
{
  CVDisplayLinkRef display_link;
}

@property (nonatomic) int display_id;
@property (nonatomic) int key_modifier_flags;
@property (nonatomic) NSOpenGLContext* gl_context;

+ (NSOpenGLContext*) createSharedOpenGLContext;

- (void)   activateDisplayLink;
- (NSPoint)convertEventPositionToLocalCoordinates:(NSEvent*)event;
- (void)   drawDisplay:(int)display_id withSize:(NSSize)display_size;

@end
