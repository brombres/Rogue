//
//  BardAppDelegate.h
//  Bard Dragon-iOS
//
//  Created by Abe Pralle on 11/6/13.
//  Copyright (c) 2013 Plasmaworks. All rights reserved.
//

#import <UIKit/UIKit.h>

#include "Bard.h"

extern BardVM* Bard_vm;

@interface BardAppDelegate : UIResponder <UIApplicationDelegate>
{
}

@property (strong, nonatomic) UIWindow *window;

@end
