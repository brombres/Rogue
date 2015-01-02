#ifndef SUPER_UI_WINDOWS_H
#define SUPER_UI_WINDOWS_H

#include "SuperUI.h"
#include <windows.h>

SUPER_BEGIN_HEADER

//=============================================================================
// SuperUIWindowInfo
//=============================================================================
typedef struct SuperUIWindowInfo
{
  int                 window_id;
  void*               user_data;

  HWND                window_handle;
  int                 width;
  int                 height;
} SuperUIWindowInfo;

SuperUIWindowInfo* SuperUIWindowInfo_create( HWND window_handle, void* user_data );


//=============================================================================
// SuperUI
//=============================================================================
void SuperUI_configure_windows( HINSTANCE hInstance, HINSTANCE hPrevInstance, const char* lpCmdLine, int nCmdShow );

SUPER_END_HEADER

#endif // SUPER_UI_WINDOWS_H
