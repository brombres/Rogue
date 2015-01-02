#ifndef SUPER_EVENT_SYSTEM_H
#define SUPER_EVENT_SYSTEM_H

#include "Super.h"

SUPER_BEGIN_HEADER

#define SUPER_EVENT_DRAW             0
#define SUPER_EVENT_POINTER_FOCUS    1
#define SUPER_EVENT_POINTER_MOVEMENT 2
#define SUPER_EVENT_POINTER_PRESS    3
#define SUPER_EVENT_POINTER_RELEASE  4
#define SUPER_EVENT_SCROLL           5
#define SUPER_EVENT_KEY_PRESS        6
#define SUPER_EVENT_KEY_RELEASE      7

typedef struct SuperEvent
{
  int   type;
  void* context;

  union
  {
    struct
    {
      int display_id;
      int display_width, display_height;
    } draw;

    struct
    {
      int display_id;
      int focus_gained;
    } pointer_focus;

    struct
    {
      int    display_id;
      double x;
      double y;
    } pointer_movement;

    struct
    {
      int    display_id;
      double x;
      double y;
      int    button_index;
    } pointer_press;

    struct
    {
      int    display_id;
      double x;
      double y;
      int    button_index;
    } pointer_release;

    struct
    {
      int    display_id;
      double dx;
      double dy;
    } scroll;

    struct
    {
      int    display_id;
      int    unicode;
      int    keycode;
      int    syscode;
      int    is_repeat;
    } key_press;

    struct
    {
      int    display_id;
      int    unicode;
      int    keycode;
      int    syscode;
    } key_release;
  };
} SuperEvent;

typedef void (*SuperEventHandler)( SuperEvent* e );

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================
void SuperEventSystem_configure( SuperEventHandler callback, void* context );

int  SuperEventSystem_dispatch( SuperEvent* event );

int  SuperEventSystem_dispatch_draw( int display_id, int display_width, int display_height );
int  SuperEventSystem_dispatch_pointer_focus(    int display_id, int focus_gained );
int  SuperEventSystem_dispatch_pointer_movement( int display_id, double x, double y );
int  SuperEventSystem_dispatch_pointer_press(    int display_id, double x, double y, int button_index );
int  SuperEventSystem_dispatch_pointer_release(  int display_id, double x, double y, int button_index );
int  SuperEventSystem_dispatch_scroll( int display_id, double dx, double dy );
int  SuperEventSystem_dispatch_key_press(   int display_id, int unicode, int keycode, int syscode, int is_repeat );
int  SuperEventSystem_dispatch_key_release( int display_id, int unicode, int keycode, int syscode );

#define SUPER_KEY_MODIFIER_SHIFT        1
#define SUPER_KEY_MODIFIER_CTRL         2
#define SUPER_KEY_MODIFIER_ALT          4
#define SUPER_KEY_MODIFIER_CAPS_LOCK    8
#define SUPER_KEY_MODIFIER_COMMAND     16

#define SUPER_KEYCODE_LEFT_ARROW        1
#define SUPER_KEYCODE_UP_ARROW          2
#define SUPER_KEYCODE_RIGHT_ARROW       3
#define SUPER_KEYCODE_DOWN_ARROW        4

#define SUPER_KEYCODE_BACKSPACE         8
#define SUPER_KEYCODE_TAB               9
#define SUPER_KEYCODE_ENTER            10

#define SUPER_KEYCODE_CAPS_LOCK        11
#define SUPER_KEYCODE_LEFT_SHIFT       12
#define SUPER_KEYCODE_RIGHT_SHIFT      13
#define SUPER_KEYCODE_LEFT_CONTROL     14
#define SUPER_KEYCODE_RIGHT_CONTROL    15
#define SUPER_KEYCODE_LEFT_ALT         16
#define SUPER_KEYCODE_RIGHT_ALT        17

// Windows/Command key
#define SUPER_KEYCODE_LEFT_OS          18
#define SUPER_KEYCODE_RIGHT_OS         19

#define SUPER_KEYCODE_FUNCTION         26
#define SUPER_KEYCODE_ESCAPE           27

#define SUPER_KEYCODE_SPACE            32

#define SUPER_KEYCODE_APOSTROPHE       39
#define SUPER_KEYCODE_COMMA            44
#define SUPER_KEYCODE_MINUS            45
#define SUPER_KEYCODE_PERIOD           46
#define SUPER_KEYCODE_SLASH            47
#define SUPER_KEYCODE_0                48
#define SUPER_KEYCODE_1                49
#define SUPER_KEYCODE_2                50
#define SUPER_KEYCODE_3                51
#define SUPER_KEYCODE_4                52
#define SUPER_KEYCODE_5                53
#define SUPER_KEYCODE_6                54
#define SUPER_KEYCODE_7                55
#define SUPER_KEYCODE_8                56
#define SUPER_KEYCODE_9                57
#define SUPER_KEYCODE_SEMICOLON        59
#define SUPER_KEYCODE_EQUALS           61

#define SUPER_KEYCODE_A                65
#define SUPER_KEYCODE_B                66
#define SUPER_KEYCODE_C                67
#define SUPER_KEYCODE_D                68
#define SUPER_KEYCODE_E                69
#define SUPER_KEYCODE_F                70
#define SUPER_KEYCODE_G                71
#define SUPER_KEYCODE_H                72
#define SUPER_KEYCODE_I                73
#define SUPER_KEYCODE_J                74
#define SUPER_KEYCODE_K                75
#define SUPER_KEYCODE_L                76
#define SUPER_KEYCODE_M                77
#define SUPER_KEYCODE_N                78
#define SUPER_KEYCODE_O                79
#define SUPER_KEYCODE_P                80
#define SUPER_KEYCODE_Q                81
#define SUPER_KEYCODE_R                82
#define SUPER_KEYCODE_S                83
#define SUPER_KEYCODE_T                84
#define SUPER_KEYCODE_U                85
#define SUPER_KEYCODE_V                86
#define SUPER_KEYCODE_W                87
#define SUPER_KEYCODE_X                88
#define SUPER_KEYCODE_Y                89
#define SUPER_KEYCODE_Z                90

#define SUPER_KEYCODE_OPEN_BRACKET     91
#define SUPER_KEYCODE_BACKSLASH        92
#define SUPER_KEYCODE_CLOSE_BRACKET    93
#define SUPER_KEYCODE_BACKQUOTE        96

#define SUPER_KEYCODE_NUMPAD_ENTER    110

#define SUPER_KEYCODE_SYS_REQUEST     124
#define SUPER_KEYCODE_SCROLL_LOCK     125
#define SUPER_KEYCODE_BREAK           126

#define SUPER_KEYCODE_DELETE          127
#define SUPER_KEYCODE_INSERT          128
#define SUPER_KEYCODE_HOME            129
#define SUPER_KEYCODE_END             130
#define SUPER_KEYCODE_PAGE_UP         131
#define SUPER_KEYCODE_PAGE_DOWN       132

#define SUPER_KEYCODE_NUMPAD_ASTERISK 142
#define SUPER_KEYCODE_NUMPAD_PLUS     143
#define SUPER_KEYCODE_NUMPAD_MINUS    145
#define SUPER_KEYCODE_NUMPAD_PERIOD   146
#define SUPER_KEYCODE_NUMPAD_SLASH    147
#define SUPER_KEYCODE_NUMPAD_0        148
#define SUPER_KEYCODE_NUMPAD_1        149
#define SUPER_KEYCODE_NUMPAD_2        150
#define SUPER_KEYCODE_NUMPAD_3        151
#define SUPER_KEYCODE_NUMPAD_4        152
#define SUPER_KEYCODE_NUMPAD_5        153
#define SUPER_KEYCODE_NUMPAD_6        154
#define SUPER_KEYCODE_NUMPAD_7        155
#define SUPER_KEYCODE_NUMPAD_8        156
#define SUPER_KEYCODE_NUMPAD_9        157
#define SUPER_KEYCODE_NUMPAD_NUM_LOCK 158
#define SUPER_KEYCODE_NUMPAD_EQUALS   161

#define SUPER_KEYCODE_F1              201
#define SUPER_KEYCODE_F2              202
#define SUPER_KEYCODE_F3              203
#define SUPER_KEYCODE_F4              204
#define SUPER_KEYCODE_F5              205
#define SUPER_KEYCODE_F6              206
#define SUPER_KEYCODE_F7              207
#define SUPER_KEYCODE_F8              208
#define SUPER_KEYCODE_F9              209
#define SUPER_KEYCODE_F10             210
#define SUPER_KEYCODE_F11             211
#define SUPER_KEYCODE_F12             212
#define SUPER_KEYCODE_F13             213
#define SUPER_KEYCODE_F14             214
#define SUPER_KEYCODE_F15             215
#define SUPER_KEYCODE_F16             216
#define SUPER_KEYCODE_F17             217
#define SUPER_KEYCODE_F18             218
#define SUPER_KEYCODE_F19             219

SUPER_END_HEADER

#endif // SUPER_EVENT_SYSTEM_H
