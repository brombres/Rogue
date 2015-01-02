#include "SuperUI-Windows.h"
#include "SuperGraphics.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <windowsx.h>
#include <CommCtrl.h>

//=============================================================================
// SuperUIWindowInfo
//=============================================================================
SuperUIWindowInfo* SuperUIWindowInfo_create( HWND window_handle, void* user_data )
{
  SuperUIWindowInfo* w = Super_allocate( SuperUIWindowInfo );
  w->window_handle = window_handle;
  w->user_data = user_data;
  return w;
}

//=============================================================================
// SuperUI
//=============================================================================

//-----------------------------------------------------------------------------
// PROTOTYPES
//-----------------------------------------------------------------------------
LRESULT CALLBACK SuperUI_window_message_handler( HWND window_handle, UINT message, WPARAM wParam, LPARAM lParam );

//-----------------------------------------------------------------------------
// GLOBALS
//-----------------------------------------------------------------------------
int                SuperUI_configured = 0;
SuperResourceBank* SuperUI_resources = NULL;
SuperPointerList*  SuperUI_window_info_list = NULL;

HINSTANCE   SuperUI_instance_handle = NULL;
HINSTANCE   SuperUI_previous_instance_handle = NULL;
const char* SuperUI_command_line;
int         SuperUI_show_command;


//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS
//-----------------------------------------------------------------------------
void SuperUI_configure_windows( HINSTANCE hInstance, HINSTANCE hPrevInstance, const char* lpCmdLine, int nCmdShow )
{
  SuperUI_instance_handle = hInstance;
  SuperUI_previous_instance_handle = hPrevInstance;
  SuperUI_command_line = lpCmdLine;
  SuperUI_show_command = nCmdShow;
}

void  SuperUI_configure()
{
  if (SuperUI_configured) return;

  SuperUI_resources = SuperResourceBank_create();
  SuperUI_window_info_list = SuperPointerList_create();

  //BardUI_screen_hdc = CreateCompatibleDC( NULL );

  //BardUI_active_font = GetStockObject(SYSTEM_FONT);
  //SelectObject( BardUI_screen_hdc, BardUI_active_font );

  {
    // Register default window class
    WNDCLASSEX def;

    def.cbSize = sizeof(WNDCLASSEX);

    def.style         = 0; //CS_HREDRAW | CS_VREDRAW;
    def.lpfnWndProc   = SuperUI_window_message_handler;
    def.cbClsExtra    = 0;
    def.cbWndExtra    = 8;  // 8 bytes of extra storage for Get/SetWindowLongPtr to store Window ID.
    def.hInstance     = SuperUI_instance_handle;
    def.hIcon         = NULL; //LoadIcon(SuperUI_instance_handle, MAKEINTRESOURCE(IDI_DECISIONTREE));
    def.hCursor       = LoadCursor( NULL, IDC_ARROW );
    def.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    def.lpszMenuName  = NULL; //MAKEINTRESOURCE(IDC_DECISIONTREE);
    def.lpszClassName = "SuperWindow";
    def.hIconSm       = NULL; //LoadIcon( SuperUI_instance_handle, MAKEINTRESOURCE(IDI_SMALL) );

    RegisterClassEx( &def );
  }

  SuperUI_configured = 1;
}

int   SuperUI_create_window( const char* title, int x, int y, int w, int h, int flags, void* user_data  )
{
  int  style = WS_OVERLAPPEDWINDOW;
  int  has_menu = 0;
  HWND window_handle;
  int  window_id;
  SuperUIWindowInfo* window_info;

  //if (flags & BARD_UI_WINDOW_STYLE_MAXIMIZE) style |= WS_MAXIMIZE;

  if (x == -1)
  {
    // Center x
    RECT r;
    SystemParametersInfo( SPI_GETWORKAREA, 0, &r, 0 );
    x = r.left + (((r.right - r.left) - w) / 2);
  }

  if (y == -1)
  {
    // Center y
    RECT r;
    SystemParametersInfo( SPI_GETWORKAREA, 0, &r, 0 );
    y = r.top + (((r.bottom - r.top) - h) / 2);
  }


  {
    // Turn content rect into frame rect
    RECT r;
    r.left = x;
    r.top = y;
    r.right = x + w;
    r.bottom = y + h;
    AdjustWindowRect( &r, style, has_menu );

    x = r.left;
    y = r.top;
    w = r.right - r.left;
    h = r.bottom - r.top;
  }

  window_handle = CreateWindowEx( 0, "SuperWindow", title, style,
     x, y, w, h,
     NULL, NULL, SuperUI_instance_handle, NULL );

  if ( !window_handle ) return 0;

  window_info = SuperUIWindowInfo_create( window_handle, user_data );
  window_info->width = w;
  window_info->height = h;

  SuperPointerList_add( SuperUI_window_info_list, window_info );
  window_id = SuperResourceBank_add( SuperUI_resources, window_handle, window_info );
  window_info->window_id = window_id;

  BringWindowToTop( window_handle );
  //ShowWindow( window_handle, SW_SHOW );
  //UpdateWindow( window_handle );

  return window_id;
}


void  SuperUI_draw_all_windows()
{
  int i;
  for (i=0; i<SuperUI_window_info_list->count; ++i)
  {
    SuperUI_draw_window( ((SuperUIWindowInfo*) SuperUI_window_info_list->data[i])->window_id );
  }
}


void  SuperUI_draw_window( int window_id )
{
  SuperUIWindowInfo* window_info = (SuperUIWindowInfo*) SuperResourceBank_get_info( SuperUI_resources, window_id );

  RECT bounds;
  GetClientRect( window_info->window_handle, &bounds );

  window_info->width  = (bounds.right - bounds.left);
  window_info->height = (bounds.bottom - bounds.top);
  SuperGraphics_draw_window( window_info );
}


void  SuperUI_get_window_content_size( int window_id, int* width, int *height )
{
  HWND window_handle = (HWND) SuperResourceBank_get_resource( SuperUI_resources, window_id );
  RECT r;

  GetClientRect( window_handle, &r );

  *width  = r.right;
  *height = r.bottom;
}

void* SuperUI_get_window_user_data( int window_id )
{
  SuperUIWindowInfo* info = (SuperUIWindowInfo*) SuperResourceBank_get_info( SuperUI_resources, window_id );
  if (info) return info->user_data;
  return NULL;
}

void SuperUI_show_window( int window_id, int visible )
{
  SuperUIWindowInfo* info = (SuperUIWindowInfo*) SuperResourceBank_get_info( SuperUI_resources, window_id );
  if (!info) return;

  if (visible)
  {
    ShowWindow( info->window_handle, SW_SHOW );
    UpdateWindow( info->window_handle );
  }
  else
  {
    ShowWindow( info->window_handle, SW_HIDE );
  }
}


LRESULT CALLBACK SuperUI_window_message_handler(HWND window_handle, UINT message, WPARAM wParam, LPARAM lParam)
{
  int window_id = SuperResourceBank_find_id( SuperUI_resources, window_handle );
  //int wm_id, wm_event;
  //int window_id = BardResourceBank_find( &BardUI_resource_bank, window_handle );
  //BardVM* vm = BardUI_vm;

  switch (message)
  {
    /*
    case WM_COMMAND:
      wm_id    = LOWORD(wParam);
      wm_event = HIWORD(wParam);

      // Parse the menu selections:
      switch (wm_id)
      {
        default:
          return DefWindowProc(window_handle, message, wParam, lParam);
      }
      break;
      */

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT:
      {
        PAINTSTRUCT ps;
        SuperUIWindowInfo* window_info = SuperResourceBank_get_info( SuperUI_resources, window_id );

        //OutputDebugString( "------------- PAINT ---------------\n" );

        // Call Begin/EndPaint to clear the dirty flag
        BeginPaint( window_handle, &ps );
        EndPaint( window_handle, &ps );
        SuperUI_draw_window( window_id );

        /*
          RECT r;
          POINT top_left;
          PAINTSTRUCT paint_struct;

          GetClientRect( window_handle, &r );

          top_left.x = r.left;
          top_left.y = r.top;
          ClientToScreen( window_handle, &top_left );
          */
      }
      break;

      /*
    case WM_SIZE:
      {
        RECT r;
        GetClientRect( window_handle, &r );

        BardVM_queue_message( vm, "UIComponent.resize" );
        BardVM_queue_message_arg( vm, "id", "%d", window_id );
        BardVM_queue_message_arg( vm, "x", "%d", r.left );
        BardVM_queue_message_arg( vm, "y", "%d", r.top );
        BardVM_queue_message_arg( vm, "width", "%d", r.right );
        BardVM_queue_message_arg( vm, "height", "%d", r.bottom );
        BardVM_dispatch_messages( vm );
      }
      break;
      */

    case WM_CLOSE:
      {
        //BardVM_queue_message( vm, "UIComponent.close" );
        //BardVM_queue_message_arg( vm, "id", "%d", window_id );
        //BardVM_dispatch_messages( vm );

        PostQuitMessage(0);
      }
      break;

    case WM_LBUTTONDOWN:
      SetCapture( window_handle );
      {
        SuperEvent event;
        event.type = SUPER_EVENT_POINTER_PRESS;
        event.pointer_press.window_id = window_id;
        event.pointer_press.x = GET_X_LPARAM(lParam);
        event.pointer_press.y = GET_Y_LPARAM(lParam);
        event.pointer_press.button_index = 0;
        SuperEventSystem_dispatch( &event );
      }
      break;

    case WM_LBUTTONUP:
      ReleaseCapture();
      {
        SuperEvent event;
        event.type = SUPER_EVENT_POINTER_RELEASE;
        event.pointer_release.window_id = window_id;
        event.pointer_release.x = GET_X_LPARAM(lParam);
        event.pointer_release.y = GET_Y_LPARAM(lParam);
        event.pointer_release.button_index = 0;
        SuperEventSystem_dispatch( &event );
      }
      break;

    case WM_RBUTTONDOWN:
      SetCapture( window_handle );
      {
        SuperEvent event;
        event.type = SUPER_EVENT_POINTER_PRESS;
        event.pointer_press.window_id = window_id;
        event.pointer_press.x = GET_X_LPARAM(lParam);
        event.pointer_press.y = GET_Y_LPARAM(lParam);
        event.pointer_press.button_index = 1;
        SuperEventSystem_dispatch( &event );
      }
      break;

    case WM_RBUTTONUP:
      ReleaseCapture();
      {
        SuperEvent event;
        event.type = SUPER_EVENT_POINTER_RELEASE;
        event.pointer_release.window_id = window_id;
        event.pointer_release.x = GET_X_LPARAM(lParam);
        event.pointer_release.y = GET_Y_LPARAM(lParam);
        event.pointer_release.button_index = 1;
        SuperEventSystem_dispatch( &event );
      }
      break;

    case WM_MOUSEMOVE:
      {
        SuperEvent event;
        event.type = SUPER_EVENT_POINTER_MOVEMENT;
        event.pointer_movement.window_id = window_id;
        event.pointer_movement.x = GET_X_LPARAM(lParam);
        event.pointer_movement.y = GET_Y_LPARAM(lParam);
        SuperEventSystem_dispatch( &event );
      }
      break;

    case WM_KEYDOWN:
      {
        WCHAR unicode_buffer[8];
        BYTE key_state[256];    
        int keycode = wParam;
        int unicode = 0;

        SuperEvent event;
        event.type = SUPER_EVENT_KEY_PRESS;
        event.key_press.window_id = window_id;
        if ((lParam & 0x40000000)) event.key_press.is_repeat = 1;
        else                       event.key_press.is_repeat = 0;
        event.key_press.syscode = keycode;

        GetKeyboardState( key_state );   
        if (1 == ToUnicode(wParam,((lParam>>16)&255),key_state,unicode_buffer,8,0))
        {
          unicode = unicode_buffer[0];
        }

        if (keycode == 13) keycode = unicode = 10;

        event.key_press.unicode = unicode;
        event.key_press.keycode = keycode;

        SuperEventSystem_dispatch( &event );
      }
      break;

    case WM_KEYUP:
      {
        WCHAR unicode_buffer[8];
        BYTE key_state[256];    
        int keycode = wParam;
        int unicode = 0;

        SuperEvent event;
        event.type = SUPER_EVENT_KEY_RELEASE;
        event.key_release.window_id = window_id;
        event.key_release.syscode = keycode;

        GetKeyboardState( key_state );   
        if (1 == ToUnicode(wParam,((lParam>>16)&255),key_state,unicode_buffer,8,0))
        {
          unicode = unicode_buffer[0];
        }

        if (keycode == 13) keycode = unicode = 10;

        event.key_release.unicode = unicode;
        event.key_release.keycode = keycode;

        SuperEventSystem_dispatch( &event );
      }
      break;

      /*
    case WM_MOUSEWHEEL:
      {
        POINT mouse_position;
        GetCursorPos ( &mouse_position );
        window_handle = WindowFromPoint( mouse_position );

        return 0;
      }

    case WM_TIMER:
      {
        BardVM_update( vm );
        //InvalidateRect( window_handle, NULL, FALSE );
      }
      break;
      */

    default:
      return DefWindowProc(window_handle, message, wParam, lParam);
  }

  return 0;
}

