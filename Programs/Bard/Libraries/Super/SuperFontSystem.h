#ifndef SUPER_FONT_SYSTEM_H
#define SUPER_FONT_SYSTEM_H

#include "Super.h"

SUPER_BEGIN_HEADER

void SuperFontSystem_configure();
void SuperFontSystem_retire();

int  SuperFontSystem_load_font_face( const char* filepath );

SuperInteger* SuperFontSystem_render_character( int font_face_id, int unicode, int* pixel_width, int* pixel_height, 
                                                int* offset_x, int* offset_y, int* advance_x, int* advance_y );
// Returns 32-bit ARGB data with variable alpha and RGB=0xffFFff, of size *pixel_width by *pixel_height.
// Returned data should be freed after using with SUPER_FREE( data );

SUPER_END_HEADER

#endif // SUPER_FONT_SYSTEM_H
