#include "SuperEventSystem.h"

//=============================================================================
// GLOBALS
//=============================================================================
SuperEventHandler SuperEventSystem_event_handler = NULL;
void*             SuperEventSystem_event_handler_context = NULL;


//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================
void SuperEventSystem_configure( SuperEventHandler callback, void* context )
{
  SuperEventSystem_event_handler = callback;
  SuperEventSystem_event_handler_context = context;
}

int SuperEventSystem_dispatch( SuperEvent* event )
{
  if ( !SuperEventSystem_event_handler ) return 0;

  event->context = SuperEventSystem_event_handler_context;
  SuperEventSystem_event_handler( event );

  return 1;
}

int SuperEventSystem_dispatch_draw( int display_id, int display_width, int display_height )
{
  SuperEvent event;
  event.type                = SUPER_EVENT_DRAW;
  event.draw.display_id     = display_id;
  event.draw.display_width  = display_width;
  event.draw.display_height = display_height;
  return SuperEventSystem_dispatch( &event );
}

int SuperEventSystem_dispatch_pointer_focus( int display_id, int focus_gained )
{
  SuperEvent event;
  event.type                       = SUPER_EVENT_POINTER_FOCUS;
  event.pointer_focus.display_id   = display_id;
  event.pointer_focus.focus_gained = focus_gained;
  return SuperEventSystem_dispatch( &event );
}

int SuperEventSystem_dispatch_pointer_movement( int display_id, double x, double y )
{
  SuperEvent event;
  event.type                        = SUPER_EVENT_POINTER_MOVEMENT;
  event.pointer_movement.display_id = display_id;
  event.pointer_movement.x          = x;
  event.pointer_movement.y          = y;
  return SuperEventSystem_dispatch( &event );
}

int SuperEventSystem_dispatch_pointer_press( int display_id, double x, double y, int button_index )
{
  SuperEvent event;
  event.type                       = SUPER_EVENT_POINTER_PRESS;
  event.pointer_press.display_id   = display_id;
  event.pointer_press.x            = x;
  event.pointer_press.y            = y;
  event.pointer_press.button_index = button_index;
  return SuperEventSystem_dispatch( &event );
}

int SuperEventSystem_dispatch_pointer_release(  int display_id, double x, double y, int button_index )
{
  SuperEvent event;
  event.type                        = SUPER_EVENT_POINTER_RELEASE;
  event.pointer_release.display_id  = display_id;
  event.pointer_release.x           = x;
  event.pointer_release.y           = y;
  event.pointer_release.button_index = button_index;
  return SuperEventSystem_dispatch( &event );
}

int SuperEventSystem_dispatch_scroll( int display_id, double dx, double dy )
{
  SuperEvent event;
  event.type              = SUPER_EVENT_SCROLL;
  event.scroll.display_id = display_id;
  event.scroll.dx         = dx;
  event.scroll.dy         = dy;
  return SuperEventSystem_dispatch( &event );
}

int SuperEventSystem_dispatch_key_press( int display_id, int unicode, int keycode, int syscode, int is_repeat )
{
  SuperEvent event;
  event.type                 = SUPER_EVENT_KEY_PRESS;
  event.key_press.display_id = display_id;
  event.key_press.unicode    = unicode;
  event.key_press.keycode    = keycode;
  event.key_press.syscode    = syscode;
  event.key_press.is_repeat  = is_repeat;
  return SuperEventSystem_dispatch( &event );
}

int SuperEventSystem_dispatch_key_release( int display_id, int unicode, int keycode, int syscode )
{
  SuperEvent event;
  event.type                   = SUPER_EVENT_KEY_RELEASE;
  event.key_release.display_id = display_id;
  event.key_release.unicode    = unicode;
  event.key_release.keycode    = keycode;
  event.key_release.syscode    = syscode;
  return SuperEventSystem_dispatch( &event );
}

