//=============================================================================
// SuperFontSystem-FreeType.c
//=============================================================================

#include "SuperFontSystem.h"

#include "ft2build.h"
#include FT_FREETYPE_H

//-----------------------------------------------------------------------------
// GLOBALS
//-----------------------------------------------------------------------------
int                SuperFontSystem_configured = 0;
FT_Library         SuperFontSystem_freetype_library;
SuperResourceBank* SuperFontSystem_font_faces = NULL;
FT_Face            SuperFontSystem_current_font_face = 0;
int                SuperFontSystem_current_character_width = 0;
int                SuperFontSystem_current_character_height = 0;


//-----------------------------------------------------------------------------
// FUNCTIONS
//-----------------------------------------------------------------------------
void SuperFontSystem_configure()
{
  if (SuperFontSystem_configured) return;
  SuperFontSystem_configured = 1;

  int error = FT_Init_FreeType( &SuperFontSystem_freetype_library );
  if ( error ) printf( "FreeType failed to initialize.\n" );

  SuperFontSystem_font_faces = SuperResourceBank_create();
}

void SuperFontSystem_retire()
{
  if ( !SuperFontSystem_configured ) return;
  SuperFontSystem_configured = 0;

  FT_Face font;
  while ((font = (FT_Face) SuperResourceBank_remove_first(SuperFontSystem_font_faces)))
  {
    FT_Done_Face( font );
  }

  SuperFontSystem_font_faces = SuperResourceBank_destroy( SuperFontSystem_font_faces );

  FT_Done_FreeType( SuperFontSystem_freetype_library );
}

int SuperFontSystem_load_font_face( const char* filepath )
{
  FT_Face font;
  int error;

  if ( !filepath ) return 0;

  SuperFontSystem_configure();

  error = FT_New_Face( SuperFontSystem_freetype_library, filepath, 0, &font );
  if (error) return 0;

  return SuperResourceBank_add( SuperFontSystem_font_faces, font, NULL );
}

SuperInteger* SuperFontSystem_render_character( int font_face_id, int unicode, int* pixel_width, int* pixel_height, 
    int* offset_x, int* offset_y, int* advance_x, int* advance_y )
{
  int error;

  FT_Face font = (FT_Face) SuperResourceBank_get_resource( SuperFontSystem_font_faces, font_face_id );
  if ( !font ) return 0;

  if (font != SuperFontSystem_current_font_face || 
      *pixel_width != SuperFontSystem_current_character_width || 
      *pixel_height != SuperFontSystem_current_character_height)
  {
    SuperFontSystem_current_font_face = font;
    SuperFontSystem_current_character_width = *pixel_width;
    SuperFontSystem_current_character_height = *pixel_height;
    FT_Set_Pixel_Sizes( font, SuperFontSystem_current_character_width, SuperFontSystem_current_character_height );
  }

  error = FT_Load_Char( font, unicode, FT_LOAD_RENDER );
  if (error) return 0;

  *pixel_width  = font->glyph->bitmap.width;
  *pixel_height = font->glyph->bitmap.rows;
  *offset_x     =  font->glyph->bitmap_left;
  *offset_y     = -font->glyph->bitmap_top;
  *advance_x    = (int) font->glyph->advance.x >> 6;
  *advance_y    = (int) font->glyph->advance.y >> 6;

  {
    int w    = *pixel_width;
    int h    = *pixel_height;
    int skip = font->glyph->bitmap.pitch - w;
    SuperInteger* data = SUPER_ALLOCATE( w * h * 4 );
    SuperInteger* dest = data - 1;
    SuperByte* src = ((SuperByte*) font->glyph->bitmap.buffer) - 1;

    int lines = h;
    while (--lines >= 0)
    {
      int count = w;
      while (--count >= 0)
      {
        int a = *(++src);
        *(++dest) = (a << 24) | 0xffFFff;
      }
      src += skip;
    }

    return data;
  }
}

