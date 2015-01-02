//
//  BardObjCInterface.m
//  BardRendererTest
//
//  Created by Michael Dwan on 10/15/13.
//  Copyright (c) 2013 AdColony. All rights reserved.
//

#import "BardObjCInterface.h"

@interface BardObjCInterface ()

@end

@implementation BardObjCInterface
+ (int)loadTexture:(NSString*)fileName intoBardTextureObject:(BardGLTexture*)texture
{
  CGImageRef spriteImage = [UIImage imageNamed:fileName].CGImage;
  if ( !spriteImage ) 
  {
    NSLog(@"Failed to load image %@", fileName);
    return 0;
  }

  size_t w = CGImageGetWidth(spriteImage);
  size_t h = CGImageGetHeight(spriteImage);

  unsigned int pow2_w = 1;
  unsigned int pow2_h = 1;
  while( pow2_h < h ) pow2_h <<= 1;
  while( pow2_w < w ) pow2_w <<= 1;

  GLubyte * spriteData = (GLubyte *) calloc(w*h*4, sizeof(GLubyte));

  CGContextRef spriteContext = CGBitmapContextCreate(spriteData, w, h, 8, w*4,
                                                     CGImageGetColorSpace(spriteImage), 
                                                     (CGBitmapInfo) kCGImageAlphaPremultipliedLast);

  CGContextDrawImage(spriteContext, CGRectMake(0, 0, w, h), spriteImage);

  CGContextRelease(spriteContext);

  GLuint texName;
  glGenTextures(1, &texName);
  glBindTexture(GL_TEXTURE_2D, texName);

  glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER, GL_LINEAR);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, spriteData);



  texture->id = texName;
  texture->image_width = (int) w;
  texture->image_height = (int) h;
  texture->texture_width = pow2_w;
  texture->texture_height = pow2_h;

  free(spriteData);
  return 1;
}

@end

int BardObjC_load_texture( char* fileName, BardGLTexture* dest )
{
  return [BardObjCInterface loadTexture:[NSString stringWithCString:fileName encoding:NSUTF8StringEncoding] intoBardTextureObject:dest];
}
