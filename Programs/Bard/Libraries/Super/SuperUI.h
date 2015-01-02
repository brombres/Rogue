#ifndef SUPER_UI_H
#define SUPER_UI_H

#include "Super.h"

SUPER_BEGIN_HEADER

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================
void  SuperUI_configure();
void  SuperUI_retire();

int   SuperUI_create_window( const char* title, int x, int y, int w, int h, int flags, void* user_data );
void  SuperUI_draw_all_windows();
void  SuperUI_draw_window( int window_id );
void  SuperUI_get_window_content_size( int window_id, int* width, int *height );
void* SuperUI_get_window_user_data( int window_id );
void  SuperUI_show_window( int window_id, int visible );

SUPER_END_HEADER

#endif // SUPER_UI_H
