#ifndef SUPER_UI_MAC_H
#define SUPER_UI_MAC_H

#import <Cocoa/Cocoa.h>

#include "Super.h"
#include "SuperUI.h"

SUPER_BEGIN_HEADER

//=============================================================================
// TYPES
//=============================================================================
typedef struct SuperUIWindowInfo
{
  int        window_id;
  void*      user_data;
  void*      window;
  int        width;
  int        height;
  int        clip_x;
  int        clip_y;
  int        clip_width;
  int        clip_height;
} SuperUIWindowInfo;

SuperUIWindowInfo* SuperUIWindowInfo_create( void* window, void* user_data );

SUPER_END_HEADER

#endif // SUPER_UI_MAC_H
