// http://www.catch22.net/tuts/scrollbars-scrolling

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <windowsx.h>
#include <CommCtrl.h>

#include "Bard.h"
#include "BardUILibrary-Windows.h"

#include "resource.h"

#define BARD_COMPONENT_TYPE_BUTTON      0
#define BARD_COMPONENT_TYPE_CHECKBOX    1
#define BARD_COMPONENT_TYPE_TAB_VIEW    2
#define BARD_COMPONENT_TYPE_LABEL       3
#define BARD_COMPONENT_TYPE_TEXT_FIELD  4
#define BARD_COMPONENT_TYPE_SCROLL_VIEW 5

#define BARD_UI_WINDOW_STYLE_MAXIMIZE   1
#define BARD_UI_WINDOW_STYLE_FULLSCREEN 3

#define BARD_UI_FONT_STYLE_NORMAL        0
#define BARD_UI_FONT_STYLE_BOLD          1
#define BARD_UI_FONT_STYLE_ITALIC        2
#define BARD_UI_FONT_STYLE_UNDERLINE     4
#define BARD_UI_FONT_STYLE_STRIKETHROUGH 8
#define BARD_UI_FONT_STYLE_MONOSPACE     16
#define BARD_UI_FONT_STYLE_PROPORTIONAL  32
#define BARD_UI_FONT_STYLE_SERIF         64
#define BARD_UI_FONT_STYLE_SANS_SERIF    128

// GLOBALS
BardVM*          BardUI_vm;
HINSTANCE        BardUI_app_handle;
HDC              BardUI_screen_hdc;
HDC              BardUI_system_paint_hdc;
//PAINTSTRUCT      BardUI_system_paint_struct;
HDC              BardUI_current_hdc;
BardResourceBank BardUI_resource_bank;

//HFONT            BardUI_active_font = 0;

HDC              BardUI_paint_buffer_hdc = 0;
HBITMAP          BardUI_paint_buffer_bitmap = 0;
HBITMAP          BardUI_paint_buffer_previous_bitmap = 0;
int              BardUI_paint_buffer_width = 0;
int              BardUI_paint_buffer_height = 0;
//BardHashTable*   BardUI_window_id_lookup;

// PROTOTYPES
void LOG( const char* message );

//=============================================================================
//  UIGraphics
//=============================================================================
void BardUIGraphics__draw_box__Real_Real_Real_Real_Integer( BardVM* vm );
void BardUIGraphics__draw_line__Real_Real_Real_Real_Integer( BardVM* vm );
void BardUIGraphics__fill_box__Real_Real_Real_Real_Integer( BardVM* vm );
//void BardUIGraphics__native_clear_clip( BardVM* vm );
void BardUIGraphics__native_create_font__String_Real_Integer( BardVM* vm );
void BardUIGraphics__native_draw_text__Integer_CharacterList_Integer_Integer_Real_Real_Integer( BardVM* vm );
void BardUIGraphics__native_measure_text_line__Integer_CharacterList_Integer_Integer( BardVM* vm );
void BardUIGraphics__native_set_clip__Real_Real_Real_Real( BardVM* vm );
//void BardUIGraphics__native_use_font__Integer( BardVM* vm );


//=============================================================================
//  UIManager
//=============================================================================
void BardUIManager__native_exit__Integer( BardVM* vm );

//=============================================================================
//  UINativeComponent
//=============================================================================
void BardUINativeComponent__native_close__Integer( BardVM* vm );
void BardUINativeComponent__native_create__Integer_String_Integer_Integer_Integer_Integer_Integer( BardVM* vm );
void BardUINativeComponent__native_request_draw__Integer_Integer_Integer_Integer_Integer( BardVM* vm );
void BardUINativeComponent__native_set_position__Integer_Integer_Integer_Integer_Integer_Logical( BardVM* vm );
void BardUINativeComponent__native_set_visible__Integer__Logical( BardVM* vm );

//=============================================================================
//  UIScrollView
//=============================================================================
//void BardUIScrollView__native_get_origin_x__Integer( BardVM* vm );
//void BardUIScrollView__native_get_origin_y__Integer( BardVM* vm );
//void BardUIScrollView__native_scroll__Integer_Integer_Integer( BardVM* vm );
//void BardUIScrollView__native_set_scroll_info__Integer_Integer_Integer_Integer_Integer( BardVM* vm );

//void BardUITabView__native_add_tab__Integer_String_Integer( BardVM* vm );
//void BardUITabView__native_retrieve_border__Integer_Integer_Integer_Integer_Integer( BardVM* vm );

void BardUIFrame__native_create__String_Integer_Integer_Integer_Integer_Integer( BardVM* vm );

void BardWindows__get_system_metric__Integer( BardVM* vm );

HDC  BardUI_begin_paint( HWND window_handle, PAINTSTRUCT* paint_struct );
void BardUI_end_paint( HWND window_handle, PAINTSTRUCT* paint_struct );

LRESULT CALLBACK BardUI_window_message_handler( HWND window_handle, UINT message, WPARAM wParam, LPARAM lParam );

void BardUILibrary_configure( BardVM* vm, HINSTANCE h_instance )
{
  BardUI_vm = vm;
  BardUI_app_handle = h_instance;

  BardUI_screen_hdc = CreateCompatibleDC( NULL );

  int pixel_height = 32;
  int point_height = -pixel_height;

  //BardUI_active_font = GetStockObject(SYSTEM_FONT);
  //SelectObject( BardUI_screen_hdc, BardUI_active_font );

  BardResourceBank_init( &BardUI_resource_bank );
  //BardUI_window_id_lookup = BardHashTable_create( 16 );

  {
    // Register default window class
    WNDCLASSEX def;

    def.cbSize = sizeof(WNDCLASSEX);

    def.style         = 0; //CS_HREDRAW | CS_VREDRAW;
    def.lpfnWndProc   = BardUI_window_message_handler;
    def.cbClsExtra    = 0;
    def.cbWndExtra    = 8;  // 8 bytes of extra storage for Get/SetWindowLongPtr to store Bard Window ID.
    def.hInstance     = BardUI_app_handle;
    def.hIcon         = NULL; //LoadIcon(BardUI_app_handle, MAKEINTRESOURCE(IDI_DECISIONTREE));
    def.hCursor       = LoadCursor( NULL, IDC_ARROW );
    def.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    def.lpszMenuName  = NULL; //MAKEINTRESOURCE(IDC_DECISIONTREE);
    def.lpszClassName = "Default";
    def.hIconSm       = NULL; //LoadIcon( BardUI_app_handle, MAKEINTRESOURCE(IDI_SMALL) );

    RegisterClassEx( &def );
  }

  BardVM_register_native_method( vm, "UICheckbox", "native_get_value(Integer)",         BardUICheckbox__native_get_value__Integer );
  BardVM_register_native_method( vm, "UICheckbox", "native_set_value(Integer,Logical)", BardUICheckbox__native_set_value__Integer__Logical );

  BardVM_register_native_method( vm, "UIFrame", "native_create(String,Integer,Integer,Integer,Integer,Integer)", 
      BardUIFrame__native_create__String_Integer_Integer_Integer_Integer_Integer );

  BardVM_register_native_method( vm, "UIGraphics", "draw_box(Real,Real,Real,Real,Integer)", 
                                                    BardUIGraphics__draw_box__Real_Real_Real_Real_Integer );
  BardVM_register_native_method( vm, "UIGraphics", "draw_line(Real,Real,Real,Real,Integer)", 
                                                    BardUIGraphics__draw_line__Real_Real_Real_Real_Integer );
  BardVM_register_native_method( vm, "UIGraphics", "native_create_font(String,Real,Integer)", 
                                                    BardUIGraphics__native_create_font__String_Real_Integer );
  BardVM_register_native_method( vm, "UIGraphics", "native_draw_text(Integer,Character[],Integer,Integer,Real,Real,Integer)", 
                                                    BardUIGraphics__native_draw_text__Integer_CharacterList_Integer_Integer_Real_Real_Integer);
  BardVM_register_native_method( vm, "UIGraphics", "fill_box(Real,Real,Real,Real,Integer)", 
                                                    BardUIGraphics__fill_box__Real_Real_Real_Real_Integer );
  BardVM_register_native_method( vm, "UIGraphics", "native_measure_text_line(Integer,Character[],Integer,Integer)", 
      BardUIGraphics__native_measure_text_line__Integer_CharacterList_Integer_Integer );
  BardVM_register_native_method( vm, "UIGraphics", "native_set_clip(Real,Real,Real,Real)", 
      BardUIGraphics__native_set_clip__Real_Real_Real_Real );
  //BardVM_register_native_method( vm, "UIGraphics", "native_use_font(Integer)", 
      //BardUIGraphics__native_use_font__Integer );

  BardVM_register_native_method( vm, "UIManager", "native_exit(Integer)", BardUIManager__native_exit__Integer );

  BardVM_register_native_method( vm, "UINativeComponent", "native_close(Integer)", BardUINativeComponent__native_close__Integer );
  BardVM_register_native_method( vm, "UINativeComponent", "native_create(Integer,String,Integer,Integer,Integer,Integer,Integer)",
                                     BardUINativeComponent__native_create__Integer_String_Integer_Integer_Integer_Integer_Integer );
  BardVM_register_native_method( vm, "UINativeComponent", "native_request_draw(Integer,Integer,Integer,Integer,Integer)",
       BardUINativeComponent__native_request_draw__Integer_Integer_Integer_Integer_Integer );
  BardVM_register_native_method( vm, "UINativeComponent", "native_set_position(Integer,Integer,Integer,Integer,Integer,Logical)", 
                                                    BardUINativeComponent__native_set_position__Integer_Integer_Integer_Integer_Integer_Logical );
  BardVM_register_native_method( vm, "UINativeComponent", "native_set_visible(Integer,Logical)", BardUINativeComponent__native_set_visible__Integer__Logical );


  /*
  BardVM_register_native_method( vm, "UIScrollView", "native_get_origin_x(Integer)", BardUIScrollView__native_get_origin_x__Integer );
  BardVM_register_native_method( vm, "UIScrollView", "native_get_origin_y(Integer)", BardUIScrollView__native_get_origin_y__Integer );
  BardVM_register_native_method( vm, "UIScrollView", "native_scroll(Integer,Integer,Integer)",
    BardUIScrollView__native_scroll__Integer_Integer_Integer );
  BardVM_register_native_method( vm, "UIScrollView", "native_set_scroll_info(Integer,Integer,Integer,Integer,Integer)",
    BardUIScrollView__native_set_scroll_info__Integer_Integer_Integer_Integer_Integer );

  BardVM_register_native_method( vm, "UITabView", "native_add_tab(Integer,String,Integer)", BardUITabView__native_add_tab__Integer_String_Integer );
  BardVM_register_native_method( vm, "UITabView", "native_retrieve_border(Integer,Integer,Integer,Integer,Integer)",
                                     BardUITabView__native_retrieve_border__Integer_Integer_Integer_Integer_Integer );
  */

  BardVM_register_native_method( vm, "Windows", "get_system_metric()", BardWindows__get_system_metric__Integer );
}


//=============================================================================
//  UICheckbox
//=============================================================================
void BardUICheckbox__native_get_value__Integer( BardVM* vm )
{
  HWND window_handle = (HWND) BardResourceBank_get( &BardUI_resource_bank, BardVM_pop_integer(vm) );

  if (window_handle) vm->sp->integer = (BST_CHECKED == Button_GetCheck(window_handle) );
  else               vm->sp->integer = 0;
}

void BardUICheckbox__native_set_value__Integer__Logical( BardVM* vm )
{
  int  value = BardVM_pop_integer( vm );
  HWND window_handle = (HWND) BardResourceBank_get( &BardUI_resource_bank, BardVM_pop_integer(vm) );
  BardVM_pop_discard( vm );  // Window object context

  if (window_handle)
  {
    Button_SetCheck( window_handle, (value ? BST_CHECKED : BST_UNCHECKED) );
  }
}



//=============================================================================
//  UIComponent
//=============================================================================
static HWND BardUIComponent_create_hwnd( const char* window_class_name, const char* title,
    int style, int x, int y, int w, int h, HWND parent_window )
{
  HWND window_handle = CreateWindowEx( 0, window_class_name, title, style,
     x, y, w, h,
     parent_window, NULL, BardUI_app_handle, NULL );

  if ( !window_handle ) return 0;

  BringWindowToTop( window_handle );

  return window_handle;
}

static int BardUIComponent_create( const char* window_class_name, const char* title,
    int style, int x, int y, int w, int h, HWND parent_window )
{
  return BardResourceBank_add( &BardUI_resource_bank,
    BardUIComponent_create_hwnd( window_class_name, title, style, x, y, w, h, parent_window ) );
}

void BardUINativeComponent__native_create__Integer_String_Integer_Integer_Integer_Integer_Integer( BardVM* vm )
{
  char label[80];
  const char* class_name;
  int  style = 0;

  HWND parent_window = (HWND) BardResourceBank_get( &BardUI_resource_bank, BardVM_pop_integer(vm) );
  int h = BardVM_pop_integer( vm );
  int w = BardVM_pop_integer( vm );
  int y = BardVM_pop_integer( vm );
  int x = BardVM_pop_integer( vm );
  BardString* label_obj = BardVM_pop_string( vm );
  int type = BardVM_pop_integer( vm );

  BardVM_pop_discard( vm );

  BardString_to_c_string( label_obj, label, 80 );

  switch (type)
  {
    case BARD_COMPONENT_TYPE_BUTTON:
      class_name = "button";
      style = BS_PUSHBUTTON | WS_CHILD;
      break;

    case BARD_COMPONENT_TYPE_CHECKBOX:
      class_name = "button";
      style = BS_AUTOCHECKBOX | WS_CHILD;
      break;

    case BARD_COMPONENT_TYPE_TAB_VIEW:
      class_name = WC_TABCONTROL;
      style = WS_CHILD;
      break;

    case BARD_COMPONENT_TYPE_LABEL:
      class_name = WC_STATIC;
      style = WS_CHILD | SS_LEFTNOWORDWRAP;
      break;

    case BARD_COMPONENT_TYPE_TEXT_FIELD:
      class_name = WC_EDIT;
      style = WS_CHILD | ES_LEFT | WS_BORDER;
      break;

    case BARD_COMPONENT_TYPE_SCROLL_VIEW:
      class_name = "Default";
      style = WS_CHILD | WS_BORDER | WS_HSCROLL | WS_VSCROLL;
      break;

    default:
      class_name = WC_STATIC;
      style = WS_CHILD | SS_LEFTNOWORDWRAP;
  }

  style |= WS_CLIPCHILDREN;

  vm->sp->integer = BardUIComponent_create( class_name, label, style, x, y, w, h, parent_window );
}

void BardUINativeComponent__native_request_draw__Integer_Integer_Integer_Integer_Integer( BardVM* vm )
{
  int h = BardVM_pop_integer( vm );
  int w = BardVM_pop_integer( vm );
  int y = BardVM_pop_integer( vm );
  int x = BardVM_pop_integer( vm );
  HWND window_handle = (HWND) BardResourceBank_get( &BardUI_resource_bank, BardVM_pop_integer(vm) );
  BardVM_pop_discard( vm );
  if (window_handle)
  {
    RECT r;
    r.left = x;
    r.top  = y;
    r.right = x + w;
    r.bottom = y + h;
    InvalidateRect( window_handle, &r, FALSE );
  }
}

void BardUINativeComponent__native_set_position__Integer_Integer_Integer_Integer_Integer_Logical( BardVM* vm )
{
  int adjust_for_frame = BardVM_pop_logical( vm );
  int h = BardVM_pop_integer( vm );
  int w = BardVM_pop_integer( vm );
  int y = BardVM_pop_integer( vm );
  int x = BardVM_pop_integer( vm );
  int window_id = BardVM_pop_integer( vm );
  HWND window_handle = (HWND) BardResourceBank_get( &BardUI_resource_bank, window_id );
  BardVM_pop_discard( vm );  // Window object context

  if (window_handle)
  {
    if (adjust_for_frame)
    {
      RECT r;
      int style = WS_OVERLAPPEDWINDOW;
      int has_menu = 0;

      r.left = x;
      r.top = y;
      r.right = x + w;
      r.bottom = y + h;

      AdjustWindowRect( &r, style, has_menu );

      x = r.left;
      y = r.top;
      w = r.right - x;
      h = r.bottom - y;
    }

    SetWindowPos( window_handle, 0, x, y, w, h, 0 );
  }
}


//=============================================================================
//  Display
//=============================================================================
void BardUI_update_display_info( BardVM* vm )
{
  BardMethod* m;

  BardType* type_Display = BardVM_find_type( vm, "Display" );
  if ( !type_Display ) return;

  m = BardType_find_method( type_Display, "update_info(Integer,Integer,Integer,Integer,Integer,Integer)" );
  if ( !m ) return;

  BardVM_push_object( vm, type_Display->singleton );
  BardVM_push_integer( vm, (BardInteger) GetSystemMetrics(SM_CXFULLSCREEN) );
  BardVM_push_integer( vm, (BardInteger) GetSystemMetrics(SM_CYFULLSCREEN) );

  {
    RECT r;
    SystemParametersInfo( SPI_GETWORKAREA, 0, &r, 0 );
    BardVM_push_integer( vm, (BardInteger) r.left );
    BardVM_push_integer( vm, (BardInteger) r.top );
    BardVM_push_integer( vm, (BardInteger) (r.right - r.left) );
    BardVM_push_integer( vm, (BardInteger) (r.bottom - r.top) );
    BardVM_invoke( vm, type_Display, m );
  }
}

//=============================================================================
//  UIGraphics
//=============================================================================
void BardUIGraphics__draw_box__Real_Real_Real_Real_Integer( BardVM* vm )
{
  BardInteger argb = BardVM_pop_integer( vm );

  int r = (argb >> 16) & 255;
  int g = (argb >> 8) & 255;
  int b = argb & 255;

  int h = (int) BardVM_pop_real( vm );
  int w = (int) BardVM_pop_real( vm );
  int y = (int) BardVM_pop_real( vm );
  int x = (int) BardVM_pop_real( vm );
  BardVM_pop_discard( vm ); // UIGraphics context

  HGDIOBJ original_pen   = SelectObject( BardUI_current_hdc, CreatePen(PS_SOLID,1,RGB(r,g,b)) );
  HGDIOBJ original_brush = SelectObject( BardUI_current_hdc, GetStockObject(NULL_BRUSH) );

  Rectangle( BardUI_current_hdc, x, y, x+w, y+h );

  SelectObject( BardUI_current_hdc, original_brush );
  DeleteObject( SelectObject(BardUI_current_hdc,original_pen) );
}

void BardUIGraphics__draw_line__Real_Real_Real_Real_Integer( BardVM* vm )
{
  BardInteger argb = BardVM_pop_integer( vm );

  int r = (argb >> 16) & 255;
  int g = (argb >> 8) & 255;
  int b = argb & 255;

  int y2 = (int) BardVM_pop_real( vm );
  int x2 = (int) BardVM_pop_real( vm );
  int y1 = (int) BardVM_pop_real( vm );
  int x1 = (int) BardVM_pop_real( vm );
  BardVM_pop_discard( vm ); // UIGraphics context

  HGDIOBJ original_pen = SelectObject( BardUI_current_hdc, CreatePen(PS_SOLID,1,RGB(r,g,b)) );

  MoveToEx( BardUI_current_hdc, x1, y1, NULL );
  LineTo( BardUI_current_hdc, x2, y2 );    // does not include (x2,y2)
  LineTo( BardUI_current_hdc, x2+1, y2 );  // draw (x2,y2)

  DeleteObject( SelectObject(BardUI_current_hdc,original_pen) );
}

//void BardUIGraphics__native_clear_clip( BardVM* vm )
//{
  //BardVM_pop_discard( vm ); // UIGraphics context
//
  //SelectClipRgn( BardUI_current_hdc, NULL );
//}

void BardUIGraphics_draw_text( HFONT font, WCHAR* text, int count, int x, int y, int fg_color )
{
  int fg_r = (fg_color >> 16) & 255;
  int fg_g = (fg_color >> 8) & 255;
  int fg_b = fg_color & 255;

  {
    HFONT original_font; 
    HDC   hdc = BardUI_paint_buffer_hdc;

    SetBkMode( hdc, TRANSPARENT );

    // Select the variable stock font into the specified device context. 
    if (original_font = (HFONT)SelectObject(hdc, font)) 
    {
      // Display the text string.  
      SetTextColor( BardUI_current_hdc, RGB(fg_r,fg_g,fg_b) );
      TextOutW( hdc, x, y, (WCHAR*) text, count ); 

      // Restore the original font.        
      SelectObject(hdc, original_font); 
    }
  }
}

void BardUIGraphics__native_create_font__String_Real_Integer( BardVM* vm )
{
  int flags  = BardVM_pop_integer( vm );
  int height = (int) (0.5 + BardVM_pop_real(vm));
  BardString* font_name_obj = BardVM_pop_string( vm );

  int bold          = (flags & BARD_UI_FONT_STYLE_BOLD) ? FW_BOLD : FW_REGULAR;
  int italic        = (flags & BARD_UI_FONT_STYLE_ITALIC) ? TRUE : FALSE;
  int underline     = (flags & BARD_UI_FONT_STYLE_UNDERLINE) ? TRUE : FALSE;
  int strikethrough = (flags & BARD_UI_FONT_STYLE_STRIKETHROUGH) ? TRUE : FALSE;
  int family        =  0;

  char  font_name[32];  // Windows fonts are limited to 32 characters
  HFONT handle;

  BardVM_pop_discard( vm ); // UIGraphics context

  BardString_to_c_string( font_name_obj, font_name, 32 );

  if (flags & BARD_UI_FONT_STYLE_MONOSPACE)         family = FIXED_PITCH;
  else if (flags & BARD_UI_FONT_STYLE_PROPORTIONAL) family = VARIABLE_PITCH;
  else                                              family = DEFAULT_PITCH;

  if (flags & BARD_UI_FONT_STYLE_SERIF)           family |= FF_ROMAN;
  else if (flags & BARD_UI_FONT_STYLE_SANS_SERIF) family |= FF_SWISS;

  handle = CreateFont(
      -height, 0,
      0, 0, 
      bold, italic, underline, strikethrough,
      ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, family, font_name );

  BardVM_push_integer( vm, BardResourceBank_add( &BardUI_resource_bank, handle ) );
}

void BardUIGraphics__native_draw_text__Integer_CharacterList_Integer_Integer_Real_Real_Integer( BardVM* vm )
{
  BardInteger fg_color = BardVM_pop_integer( vm );
  BardInteger y = (BardInteger) BardVM_pop_real( vm );
  BardInteger x = (BardInteger) BardVM_pop_real( vm );
  int i2 = BardVM_pop_integer( vm );
  int i1 = BardVM_pop_integer( vm );
  BardObject* list = BardVM_pop_object( vm );
  BardCharacter*  data = BardList_data( list )->character_data;

  int font_id = BardVM_pop_integer( vm );
  HFONT font_handle = (HFONT) BardResourceBank_get( &BardUI_resource_bank, font_id );

  BardVM_pop_discard( vm ); // UIGraphics context

  BardUIGraphics_draw_text( font_handle, (WCHAR*) data+i1, (i2-i1)+1, x, y, fg_color );
}

void BardUIGraphics__native_measure_text_line__Integer_CharacterList_Integer_Integer( BardVM* vm )
{
  int w, h;
  RECT r;
  HFONT original_font; 
  WCHAR* characters;
  WCHAR  space = ' ';

  int i2 = BardVM_pop_integer( vm );
  int i1 = BardVM_pop_integer( vm );
  BardObject* list = BardVM_pop_object( vm );
  int font_id = BardVM_pop_integer( vm );
  HFONT font_handle = (HFONT) BardResourceBank_get( &BardUI_resource_bank, font_id );

  BardVM_pop_discard( vm ); // UIGraphics context

  if (list)
  {
    characters = (WCHAR*) (BardList_data( list )->character_data);
  }
  else
  {
    // Measure the size of a single space if the Character[] list is null
    // (relied on by the controlling layer to determine line height).
    characters = &space;
    i1 = 0;
    i2 = 0;
  }

  original_font = (HFONT) SelectObject( BardUI_screen_hdc, font_handle );
  DrawTextW( BardUI_screen_hdc, characters+i1, (i2-i1)+1, &r, DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE );
  SelectObject( BardUI_screen_hdc, original_font );

  w = r.right - r.left;
  h = r.bottom - r.top;
   
  if (w < 0) w = -w;
  if (h < 0) h = -h;

  BardVM_push_real( vm, h );
  BardVM_push_real( vm, w );
}

void BardUIGraphics__native_set_clip__Real_Real_Real_Real( BardVM* vm )
{
  HRGN region;

  BardInteger h = (BardInteger) BardVM_pop_real( vm );
  BardInteger w = (BardInteger) BardVM_pop_real( vm );
  BardInteger y = (BardInteger) BardVM_pop_real( vm );
  BardInteger x = (BardInteger) BardVM_pop_real( vm );
  BardVM_pop_discard( vm ); // UIGraphics context

  region = CreateRectRgn(x,y,x+w,y+h);
  SelectClipRgn( BardUI_current_hdc, region );
  DeleteObject( region );
}


//void BardUIGraphics__native_use_font__Integer( BardVM* vm )
//{
//  HFONT handle;
//  int font_id = BardVM_pop_integer( vm );
//
//  BardVM_pop_discard( vm ); // UIGraphics context
//
//  handle = (HFONT) BardResourceBank_get( &BardUI_resource_bank, font_id );
//
//  if (handle) BardUI_active_font = handle;
//}


void BardUIGraphics__fill_box__Real_Real_Real_Real_Integer( BardVM* vm )
{
  BardInteger argb = BardVM_pop_integer( vm );

  int r = (argb >> 16) & 255;
  int g = (argb >> 8) & 255;
  int b = argb & 255;

  int h = (int) BardVM_pop_real( vm );
  int w = (int) BardVM_pop_real( vm );
  int y = (int) BardVM_pop_real( vm );
  int x = (int) BardVM_pop_real( vm );
//char st[80];
//sprintf(st,"bounds:%d,%d %dx%d",x,y,w,h);
//LOG(st);
  BardVM_pop_discard( vm ); // UIGraphics context

  HBRUSH brush = CreateSolidBrush( RGB(r,g,b) );
  RECT box;

  box.left   = x;
  box.top    = y;
  box.right  = x + w;
  box.bottom = y + h;

  FillRect( BardUI_current_hdc, &box, brush );

  DeleteObject( brush );
}


//=============================================================================
//  UIScrollView
//=============================================================================
/*
void BardUIScrollView_native_scroll( HWND window_handle, int dx, int dy )
{
  SCROLLINFO scroll_info;
  int original_x, original_y;

  scroll_info.cbSize = sizeof( SCROLLINFO );
  scroll_info.fMask  = SIF_ALL;

  // Save the position for comparison later on.
  GetScrollInfo ( window_handle, SB_HORZ, &scroll_info );
  original_x = scroll_info.nPos;

  GetScrollInfo ( window_handle, SB_VERT, &scroll_info );
  original_y = scroll_info.nPos;


  // Set the position and then retrieve it.  Due to adjustments
  // by Windows it may not be the same as the value set.
  scroll_info.fMask = SIF_POS;
  scroll_info.nPos = original_x + dx;
  SetScrollInfo( window_handle, SB_HORZ, &scroll_info, TRUE );
  GetScrollInfo( window_handle, SB_HORZ, &scroll_info);
  dx = (original_x - scroll_info.nPos);
   
  scroll_info.fMask = SIF_POS;
  scroll_info.nPos = original_y + dy;
  SetScrollInfo( window_handle, SB_VERT, &scroll_info, TRUE );
  GetScrollInfo( window_handle, SB_VERT, &scroll_info);
  dy = (original_y - scroll_info.nPos);
   
  // If the position has changed, scroll the window.
  if (dx || dy)
  {
    ScrollWindow(window_handle, dx, dy, NULL, NULL);
    UpdateWindow( window_handle );
  }
}

void BardUIScrollView__native_get_origin_x__Integer( BardVM* vm )
{
  SCROLLINFO scroll_info;

  int window_id = BardVM_pop_integer( vm );
  HWND window_handle = (HWND) BardResourceBank_get( &BardUI_resource_bank, window_id );
  BardVM_pop_discard( vm );

  scroll_info.cbSize = sizeof( SCROLLINFO );
  scroll_info.fMask  = SIF_ALL;

  // Save the position for comparison later on.
  GetScrollInfo ( window_handle, SB_HORZ, &scroll_info );
  BardVM_push_integer( vm, -((int) scroll_info.nPos) );
}

void BardUIScrollView__native_get_origin_y__Integer( BardVM* vm )
{
  SCROLLINFO scroll_info;

  int window_id = BardVM_pop_integer( vm );
  HWND window_handle = (HWND) BardResourceBank_get( &BardUI_resource_bank, window_id );
  BardVM_pop_discard( vm );

  scroll_info.cbSize = sizeof( SCROLLINFO );
  scroll_info.fMask  = SIF_ALL;

  // Save the position for comparison later on.
  GetScrollInfo ( window_handle, SB_VERT, &scroll_info );
  BardVM_push_integer( vm, -((int) scroll_info.nPos) );
}

void BardUIScrollView__native_scroll__Integer_Integer_Integer( BardVM* vm )
{
  int dy = BardVM_pop_integer( vm );
  int dx = BardVM_pop_integer( vm );
  int window_id = BardVM_pop_integer( vm );
  HWND window_handle = (HWND) BardResourceBank_get( &BardUI_resource_bank, window_id );
  BardVM_pop_discard( vm );

  BardUIScrollView_native_scroll( window_handle, dx, dy );
}

void BardUIScrollView__native_set_scroll_info__Integer_Integer_Integer_Integer_Integer( BardVM* vm )
{
  SCROLLINFO info;

  int page_h    = BardVM_pop_integer( vm );
  int page_w    = BardVM_pop_integer( vm );
  int visible_h = BardVM_pop_integer( vm );
  int visible_w = BardVM_pop_integer( vm );
  int window_id = BardVM_pop_integer( vm );
  HWND window_handle = (HWND) BardResourceBank_get( &BardUI_resource_bank, window_id );
  BardVM_pop_discard( vm );

  if (window_handle)
  {
    info.cbSize = sizeof( SCROLLINFO );
    info.fMask  = SIF_PAGE | SIF_RANGE;
    info.nMin   = 0;
    info.nMax   = page_w - 1;
    info.nPage  = visible_w;
    SetScrollInfo( window_handle, SB_HORZ, &info, TRUE );

    info.nMax   = page_h - 1;
    info.nPage  = visible_h;
    SetScrollInfo( window_handle, SB_VERT, &info, TRUE );
  }
}

//=============================================================================
//  UITabView
//=============================================================================
void BardUITabView__native_add_tab__Integer_String_Integer( BardVM *vm )
{
  int  at_index = BardVM_pop_integer( vm );
  BardString* label_obj = BardVM_pop_string( vm );
  int window_id = BardVM_pop_integer( vm );
  char label[80];
  HWND window_handle = (HWND) BardResourceBank_get( &BardUI_resource_bank, window_id );

  BardVM_pop_discard( vm );

  BardString_to_c_string( label_obj, label, 80 );

  if (window_id)
  {
    TCITEM item;
    item.mask    = TCIF_TEXT;
    item.pszText = label;
    TabCtrl_InsertItem( window_handle, at_index, &item );
  }
}

void BardUITabView__native_retrieve_border__Integer_Integer_Integer_Integer_Integer( BardVM *vm )
{
  int h = BardVM_pop_integer( vm );
  int w = BardVM_pop_integer( vm );
  int y = BardVM_pop_integer( vm );
  int x = BardVM_pop_integer( vm );
  int window_id = BardVM_pop_integer( vm );

  int left, top, right, bottom;

  HWND window_handle = (HWND) BardResourceBank_get( &BardUI_resource_bank, window_id );
  RECT r;

  r.left = x;
  r.top  = y;
  r.right = x+w;
  r.bottom = y+h;

  if (window_handle)
  {
    TabCtrl_AdjustRect( window_handle, FALSE, &r );

    left = r.left - x;
    right = (x+w) - r.right;
    top = r.top - y;
    bottom = (y+h) - r.bottom;
  }
  else
  {
    left = top = right = bottom = 0;
  }

  {
    BardType* type_UITabView = vm->sp->object->type;
    BardMethod* m = BardType_find_method( type_UITabView, "with_border(Integer,Integer,Integer,Integer)" );

    if (m)
    {
      BardVM_push_integer( vm, left );
      BardVM_push_integer( vm, top );
      BardVM_push_integer( vm, right );
      BardVM_push_integer( vm, bottom );
      BardVM_invoke( vm, type_UITabView, m );
      BardVM_pop_discard( vm );  // discard return value
    }
  }
}
*/

//=============================================================================
//  UIManager
//=============================================================================
void BardUIManager__native_exit__Integer( BardVM* vm )
{
  int error_code = BardVM_pop_integer( vm );
  BardVM_pop_discard( vm );
  PostQuitMessage( error_code );
}


//=============================================================================
//  UINativeComponent
//=============================================================================

void BardUINativeComponent__native_close__Integer( BardVM* vm )
{
  BardInteger window_id = BardVM_pop_integer( vm );
  HWND window_handle = BardResourceBank_remove( &BardUI_resource_bank, window_id );

  BardVM_pop_discard( vm );  // object context

  if (window_handle)
  {
    DestroyWindow( window_handle );
  }
}

void BardUIFrame__native_create__String_Integer_Integer_Integer_Integer_Integer( BardVM* vm )
{
  int h = BardVM_pop_integer( vm );
  int w = BardVM_pop_integer( vm );
  int y = BardVM_pop_integer( vm );
  int x = BardVM_pop_integer( vm );
  int flags = BardVM_pop_integer( vm );
  BardString* title_obj = BardVM_pop_string( vm );

  char title[80];
  int  style = WS_OVERLAPPEDWINDOW;
  int has_menu = 0;

  if (flags & BARD_UI_WINDOW_STYLE_MAXIMIZE) style |= WS_MAXIMIZE;

  BardString_to_c_string( title_obj, title, 80 );

  if (x != -1)
  {
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
  else
  {
    x = y = w = h = CW_USEDEFAULT;
  }

  vm->sp->integer = BardUIComponent_create( "Default", title, style, x, y, w, h, NULL );

  {
    HWND window_handle = BardResourceBank_get( &BardUI_resource_bank, vm->sp->integer );
    SetTimer( window_handle, 1, 1000, NULL );
  }
}

void BardUINativeComponent__native_set_visible__Integer__Logical( BardVM* vm )
{
  int setting = BardVM_pop_integer( vm );
  int window_id = BardVM_pop_integer( vm );
  HWND window_handle = (HWND) BardResourceBank_get( &BardUI_resource_bank, window_id );
  BardVM_pop_discard( vm );  // Window object context

  if (window_handle)
  {
    if (setting)
    {
      ShowWindow( window_handle, SW_SHOW );
      UpdateWindow( window_handle );
    }
    else
    {
      ShowWindow( window_handle, SW_HIDE );
      UpdateWindow( window_handle );
    }
  }
}


//=============================================================================
//  Windows
//=============================================================================
void BardWindows__get_system_metric__Integer( BardVM* vm )
{
  int what = BardVM_pop_integer( vm );
  BardVM_pop_discard( vm );
  BardVM_push_integer( vm, (BardInteger) GetSystemMetrics(what) );
}


//=============================================================================
//  BardUI
//=============================================================================
HDC BardUI_begin_paint( HWND window_handle, PAINTSTRUCT* paint_struct )
{
  RECT r;

  BardUI_system_paint_hdc = BeginPaint( window_handle, paint_struct );
  if (!BardUI_system_paint_hdc) return NULL;

  if ( !BardUI_paint_buffer_hdc ) BardUI_paint_buffer_hdc = CreateCompatibleDC( BardUI_system_paint_hdc );

  GetClientRect( window_handle, &r );

  if (r.right != BardUI_paint_buffer_width || r.bottom != BardUI_paint_buffer_height)
  {
    if (BardUI_paint_buffer_bitmap) DeleteObject( BardUI_paint_buffer_bitmap );

    BardUI_paint_buffer_bitmap = CreateCompatibleBitmap( BardUI_system_paint_hdc, r.right, r.bottom );
    BardUI_paint_buffer_previous_bitmap = SelectObject( BardUI_paint_buffer_hdc, BardUI_paint_buffer_bitmap );
  }

  //FillRect( BardUI_paint_buffer_hdc, &r, (HBRUSH)(COLOR_BACKGROUND+1) );

  return BardUI_paint_buffer_hdc;
}

void BardUI_end_paint( HWND window_handle, PAINTSTRUCT* paint_struct )
{
  RECT r;
  GetClientRect( window_handle, &r );

  //FillRect(hDCMem, &r, (HBRUSH)(COLOR_BTNFACE+1));


  BitBlt( BardUI_system_paint_hdc, 0, 0, r.right, r.bottom, BardUI_paint_buffer_hdc, 0, 0, SRCCOPY );
  SelectObject( BardUI_paint_buffer_hdc, BardUI_paint_buffer_previous_bitmap );

  EndPaint( window_handle, paint_struct );
}


//-----------------------------------------------------------------------------
//  BardUI_window_message_handler
//-----------------------------------------------------------------------------
LRESULT CALLBACK BardUI_window_message_handler(HWND window_handle, UINT message, WPARAM wParam, LPARAM lParam)
{
  int wm_id, wm_event;
  //int window_id = (int)(intptr_t) GetWindowLongPtr( window_handle, 0 );
  int window_id = BardResourceBank_find( &BardUI_resource_bank, window_handle );
  BardVM* vm = BardUI_vm;

  switch (message)
  {
    case WM_COMMAND:
      wm_id    = LOWORD(wParam);
      wm_event = HIWORD(wParam);

      /*
      switch (wm_id)
      {
        case BN_CLICKED:
          BardVM_queue_message( vm, "UIControl.on_select" );
          BardVM_queue_message_arg( vm, "id", "%d", BardResourceBank_find( &BardUI_resource_bank, (void*) lParam ) );
          BardVM_dispatch_messages( vm );
          return 0;
      }
      */

      // Parse the menu selections:
      switch (wm_id)
      {
        /*
        case IDM_ABOUT:
          //DialogBox(BardUI_app_handle, MAKEINTRESOURCE(IDD_ABOUTBOX), window_handle, About);
          break;

        case IDM_EXIT:
          DestroyWindow(window_handle);
          break;
          */

        default:
          return DefWindowProc(window_handle, message, wParam, lParam);
      }
      break;

      /*
    case WM_NOTIFY:
      wm_id    = LOWORD(wParam);
      wm_event = HIWORD(wParam);
      switch (((LPNMHDR)lParam)->code)
      {
        case TCN_SELCHANGE:
          {
            HWND tab_control_handle = ((LPNMHDR)lParam)->hwndFrom;
            BardVM_queue_message( vm, "UIControl.on_select" );
            BardVM_queue_message_arg( vm, "id", "%d", BardResourceBank_find(&BardUI_resource_bank, tab_control_handle) );
            BardVM_queue_message_arg( vm, "index", "%d", TabCtrl_GetCurSel(tab_control_handle) );
            BardVM_dispatch_messages( vm );
          }
          return 0;

        default:
          return DefWindowProc( window_handle, message, wParam, lParam );
      }
      break;
      */

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT:
      {
        BardType*   type_UI = BardVM_find_type( vm, "UIManager" );
        BardMethod* m_draw = BardType_find_method( type_UI, "draw(Integer,Integer,Integer)" );
        if (type_UI && m_draw)
        {
          RECT r;
          POINT top_left;
          PAINTSTRUCT paint_struct;

          GetClientRect( window_handle, &r );

          top_left.x = r.left;
          top_left.y = r.top;
          ClientToScreen( window_handle, &top_left );

          BardUI_current_hdc = BardUI_begin_paint( window_handle, &paint_struct );

          if (BardUI_current_hdc)
          {
            // Let Bard control the drawing
            BardVM_push_object( vm, type_UI->singleton );
            BardVM_push_integer( vm, window_id );
            //BardVM_push_integer( vm, top_left.x );
            //BardVM_push_integer( vm, top_left.y );
            BardVM_push_integer( vm, r.right );
            BardVM_push_integer( vm, r.bottom );
            BardVM_invoke( vm, type_UI, m_draw );

            BardUI_end_paint( window_handle, &paint_struct );
          }
        }
      }
      break;

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

    case WM_CLOSE:
      {
        BardVM_queue_message( vm, "UIComponent.close" );
        BardVM_queue_message_arg( vm, "id", "%d", window_id );
        BardVM_dispatch_messages( vm );

        //PostQuitMessage(0);
      }
      break;

      /*
    case WM_HSCROLL:
      {
        int dx = 0;
        SCROLLINFO scroll_info;

        scroll_info.cbSize = sizeof( SCROLLINFO );
        scroll_info.fMask  = SIF_ALL;
        GetScrollInfo ( window_handle, SB_HORZ, &scroll_info );

        switch (LOWORD(wParam))
        {
          // User clicked the left arrow.
          case SB_LINELEFT: 
            dx = -4;
            //dx = -(int)scroll_info.nPage;
            break;
                
          // User clicked the right arrow.
          case SB_LINERIGHT: 
            dx = 4;
            //dx = (int)scroll_info.nPage;
            break;
                
          // User clicked the scroll bar shaft left of the scroll box.
          case SB_PAGELEFT:
            dx = -(int)scroll_info.nPage;
            break;
                
          // User clicked the scroll bar shaft right of the scroll box.
          case SB_PAGERIGHT:
            dx = (int)scroll_info.nPage;
            break;
                
          // User dragged the scroll box.
          case SB_THUMBTRACK: 
            dx = (int)scroll_info.nTrackPos - (int)scroll_info.nPos;
            break;
        }

        BardUIScrollView_native_scroll( window_handle, dx, 0 );
      }
      return 0;

    case WM_VSCROLL:
      {
        int dy = 0;
        SCROLLINFO scroll_info;

        scroll_info.cbSize = sizeof( SCROLLINFO );
        scroll_info.fMask  = SIF_ALL;
        GetScrollInfo ( window_handle, SB_VERT, &scroll_info );

        switch (LOWORD(wParam))
        {
          // User clicked the left arrow.
          case SB_LINEUP: 
            dy = -(int)scroll_info.nPage;
            break;
                
          // User clicked the right arrow.
          case SB_LINEDOWN: 
            dy = (int)scroll_info.nPage;
            break;
                
          // User clicked the scroll bar shaft left of the scroll box.
          case SB_PAGEUP:
            dy = -(int)scroll_info.nPage;
            break;
                
          // User clicked the scroll bar shaft right of the scroll box.
          case SB_PAGEDOWN:
            dy = (int)scroll_info.nPage;
            break;
                
          // User dragged the scroll box.
          case SB_THUMBTRACK: 
            dy = (int)scroll_info.nTrackPos - (int)scroll_info.nPos;
            break;
        }

        BardUIScrollView_native_scroll( window_handle, 0, dy );
      }
      return 0;
      */

    case WM_LBUTTONDOWN:
      SetCapture( window_handle );
      BardVM_queue_message( vm, "UIComponent.on_pointer_press" );
      BardVM_queue_message_arg( vm, "component_id", "%d", window_id );
      BardVM_queue_message_arg( vm, "button_index", "0" );
      BardVM_queue_message_arg( vm, "x", "%d", GET_X_LPARAM(lParam) );
      BardVM_queue_message_arg( vm, "y", "%d", GET_Y_LPARAM(lParam) );
      BardVM_dispatch_messages( vm );
      break;

    case WM_LBUTTONUP:
      ReleaseCapture();
      BardVM_queue_message( vm, "UIComponent.on_pointer_release" );
      BardVM_queue_message_arg( vm, "component_id", "%d", window_id );
      BardVM_queue_message_arg( vm, "button_index", "0" );
      BardVM_queue_message_arg( vm, "x", "%d", GET_X_LPARAM(lParam) );
      BardVM_queue_message_arg( vm, "y", "%d", GET_Y_LPARAM(lParam) );
      BardVM_dispatch_messages( vm );
      break;

    case WM_RBUTTONDOWN:
      SetCapture( window_handle );
      BardVM_queue_message( vm, "UIComponent.on_pointer_press" );
      BardVM_queue_message_arg( vm, "component_id", "%d", window_id );
      BardVM_queue_message_arg( vm, "button_index", "1" );
      BardVM_queue_message_arg( vm, "x", "%d", GET_X_LPARAM(lParam) );
      BardVM_queue_message_arg( vm, "y", "%d", GET_Y_LPARAM(lParam) );
      BardVM_dispatch_messages( vm );
      break;

    case WM_RBUTTONUP:
      ReleaseCapture();
      BardVM_queue_message( vm, "UIComponent.on_pointer_release" );
      BardVM_queue_message_arg( vm, "component_id", "%d", window_id );
      BardVM_queue_message_arg( vm, "button_index", "1" );
      BardVM_queue_message_arg( vm, "x", "%d", GET_X_LPARAM(lParam) );
      BardVM_queue_message_arg( vm, "y", "%d", GET_Y_LPARAM(lParam) );
      BardVM_dispatch_messages( vm );
      break;

    case WM_MOUSEMOVE:
      BardVM_queue_message( vm, "UIComponent.on_pointer_move" );
      BardVM_queue_message_arg( vm, "component_id", "%d", window_id );
      BardVM_queue_message_arg( vm, "x", "%d", GET_X_LPARAM(lParam) );
      BardVM_queue_message_arg( vm, "y", "%d", GET_Y_LPARAM(lParam) );
      BardVM_dispatch_messages( vm );
      break;

    case WM_KEYDOWN:
      BardVM_queue_message( vm, "UIComponent.on_key_press" );
      BardVM_queue_message_arg( vm, "component_id", "%d", window_id );
      {
        WCHAR unicode_buffer[8];
        BYTE key_state[256];    
        int keycode = wParam;
        int unicode = 0;
 
        GetKeyboardState( key_state );   
        if (1 == ToUnicode(wParam,((lParam>>16)&255),key_state,unicode_buffer,8,0))
        {
          unicode = unicode_buffer[0];
        }

        if (keycode == 13) keycode = unicode = 10;

        BardVM_queue_message_arg( vm, "unicode", "%d", unicode );
        BardVM_queue_message_arg( vm, "keycode", "%d", keycode );
      }
      if (lParam & 0x40000000) BardVM_queue_message_arg( vm, "is_repeat", "true" );
      BardVM_dispatch_messages( vm );
      break;

    case WM_KEYUP:
      BardVM_queue_message( vm, "UIComponent.on_key_release" );
      BardVM_queue_message_arg( vm, "component_id", "%d", window_id );
      {
        WCHAR unicode_buffer[8];
        BYTE key_state[256];    
        int keycode = wParam;
        int unicode = 0;
 
        GetKeyboardState( key_state );   
        if (1 == ToUnicode(wParam,((lParam>>16)&255),key_state,unicode_buffer,8,0))
        {
          unicode = unicode_buffer[0];
        }

        if (keycode == 13) keycode = unicode = 10;

        BardVM_queue_message_arg( vm, "unicode", "%d", unicode );
        BardVM_queue_message_arg( vm, "keycode", "%d", keycode );
      }
      BardVM_dispatch_messages( vm );
      break;

    case WM_MOUSEWHEEL:
      {
        POINT mouse_position;
        GetCursorPos ( &mouse_position );
        window_handle = WindowFromPoint( mouse_position );

        /*
        while (window_handle)
        {
          int style = GetWindowLong( window_handle, GWL_STYLE );

          if (style & WS_VSCROLL)
          {
            BardUIScrollView_native_scroll( window_handle, 0, ((int) GET_WHEEL_DELTA_WPARAM(wParam)) / -4 );
          }

          window_handle = GetParent( window_handle );
        }
        */
        return 0;
      }

    case WM_TIMER:
      {
        BardVM_update( vm );
        //InvalidateRect( window_handle, NULL, FALSE );
      }
      break;

    default:
      return DefWindowProc(window_handle, message, wParam, lParam);
  }
  return 0;
}

