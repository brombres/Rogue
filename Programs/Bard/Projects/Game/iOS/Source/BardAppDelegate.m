//
//  BardAppDelegate.m
//  Bard Dragon-iOS
//
//  Created by Abe Pralle on 11/6/13.
//  Copyright (c) 2013 Plasmaworks. All rights reserved.
//

#import "BardAppDelegate.h"
#import "BardViewController.h"

#include "BardGameLibrary.h"

// GLOBALS
BardVM* Bard_vm = NULL;


@implementation BardAppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
  // Override point for customization after application launch.
  self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]] ;

  BardViewController* view_controller = [[BardViewController alloc] init];
  self.window.rootViewController = view_controller;

  [self.window makeKeyAndVisible];

  Bard_vm = BardVM_create();
  BardVM* vm = Bard_vm;

  BardStandardLibrary_configure( vm );
  BardGameLibrary_configure( vm );

  if (BardVM_load_bc_file(vm,[[[NSBundle mainBundle] pathForResource:@"Game" ofType:@"bc"] UTF8String]))
  {
    BardVM_create_main_object( vm );
  }
  else
  {
    NSLog( @"Failed to load Game.bc." );
    BardVM_free( vm );
    Bard_vm = NULL;
  }

  return YES;
}
							
- (void)applicationWillResignActive:(UIApplication *)application
{
    // Sent when the application is about to move from active to inactive state. This can occur for certain types of temporary interruptions (such as an incoming phone call or SMS message) or when the user quits the application and it begins the transition to the background state.
    // Use this method to pause ongoing tasks, disable timers, and throttle down OpenGL ES frame rates. Games should use this method to pause the game.
}

- (void)applicationDidEnterBackground:(UIApplication *)application
{
    // Use this method to release shared resources, save user data, invalidate timers, and store enough application state information to restore your application to its current state in case it is terminated later. 
    // If your application supports background execution, this method is called instead of applicationWillTerminate: when the user quits.
}

- (void)applicationWillEnterForeground:(UIApplication *)application
{
    // Called as part of the transition from the background to the inactive state; here you can undo many of the changes made on entering the background.
}

- (void)applicationDidBecomeActive:(UIApplication *)application
{
    // Restart any tasks that were paused (or not yet started) while the application was inactive. If the application was previously in the background, optionally refresh the user interface.
}

- (void)applicationWillTerminate:(UIApplication *)application
{
    // Called when the application is about to terminate. Save data if appropriate. See also applicationDidEnterBackground:.
}

@end
