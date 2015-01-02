//
//  BardObjCInterface.h
//  BardRendererTest
//
//  Created by Michael Dwan on 10/15/13.
//  Copyright (c) 2013 AdColony. All rights reserved.
//

#include "BardGL.h"


@interface BardObjCInterface : NSObject
+ (int)loadTexture:(NSString*)fileName intoBardTextureObject:(BardGLTexture*)texture;
@end

int BardObjC_load_texture( char* fileName, BardGLTexture* dest );

