//
//  BardGLView.h
//  BardRendererTest
//
//  Created by Michael Dwan on 8/8/13.
//  Copyright (c) 2013 AdColony. All rights reserved.
//

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>

#import <OpenGLES/EAGL.h>
#import <OpenGLES/EAGLDrawable.h>

@protocol BardGLViewDelegate <NSObject>
- (NSString *)executeJavascript:(NSString *)javascript, ...;
- (NSString *)executeJavascript:(NSString *)javascript withArgs:(va_list)args;
- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event;
@end

@interface BardGLView : UIView
{
  EAGLContext* context;
  CAEAGLLayer* eaglLayer;
  GLuint colorRenderBuffer;
}

@property id<BardGLViewDelegate> delegate;

@end
