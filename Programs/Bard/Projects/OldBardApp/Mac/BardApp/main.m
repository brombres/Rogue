//
//  main.m
//  BardApp
//
//  Created by Abe Pralle on 1/30/14.
//  Copyright (c) 2014 Plasmaworks. All rights reserved.
//

#import <Cocoa/Cocoa.h>

#include "SuperBardVM.h"

//#include "BardNovaLibrary.h"

extern int    BardApp_argc;
extern void** BardApp_argv;

int main(int argc, const char* argv[])
{
  SuperBardVM_set_args( argc, argv );

  return NSApplicationMain(argc, argv);
}

