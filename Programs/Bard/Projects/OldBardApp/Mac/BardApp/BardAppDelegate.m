//
//  BardAppDelegate.m
//  BardApp
//
//  Created by Abe Pralle on 1/30/14.
//  Copyright (c) 2014 Plasmaworks. All rights reserved.
//

#import "BardAppDelegate.h"

#include "SuperBardVM.h"

@implementation BardAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
  BardVM* vm = SuperBardVM_create();
  SuperBardVM_launch( vm,
      [[[NSBundle mainBundle] pathForResource:@"Program" ofType:@"bc"] UTF8String] );

  /*
  Bard_init();
  Bard_load( @"Program" );
    // Insert code here to initialize your application
    
    SDL_Window *window;                    // Declare a pointer
    
    SDL_Init(SDL_INIT_VIDEO);              // Initialize SDL2
    
    // Create an application window with the following settings:
    window = SDL_CreateWindow(
                              "An SDL2 window",                  // window title
                              SDL_WINDOWPOS_UNDEFINED,           // initial x position
                              SDL_WINDOWPOS_UNDEFINED,           // initial y position
                              640,                               // width, in pixels
                              480,                               // height, in pixels
                              SDL_WINDOW_OPENGL                  // flags - see below
                              );
    
    // Check that the window was successfully made
    if (window == NULL) {
        // In the event that the window could not be made...
        printf("Could not create window: %s\n", SDL_GetError());
    }
    
    // The window is open: enter program loop (see SDL_PollEvent)
    
    //SDL_Delay(3000);  // Pause execution for 3000 milliseconds, for example
    
    // Close and destroy the window
    //SDL_DestroyWindow(window);
    
    // Clean up
    //SDL_Quit();

    */
}

@end
