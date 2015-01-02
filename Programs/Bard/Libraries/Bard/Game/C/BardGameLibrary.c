#include <sys/syslimits.h>

#include "BardGameLibrary.h"
#include "BardGL.h"

void BardGameLibrary_configure( BardVM* vm )
{
  BardVM_register_native_method( vm, "Display", "clear(Integer)",  BardVMDisplay__clear__Integer );

  BardVM_register_native_method( vm, "Image", 
      "native_draw(Integer,Real,Real,Real,Real,Real,Real,Real,Real,Real)",  
      BardVMImage__native_draw__Integer_Real_Real_Real_Real_Real_Real_Real_Real_Real );

  BardVM_register_native_method( vm, "Texture", "native_load(String)", BardVMTexture__native_load__String );
}

//=============================================================================
//  Display
//=============================================================================
void BardVMDisplay__clear__Integer( BardVM* vm )
{
  unsigned int aaRRggBB = BardVM_pop_integer( vm );
  BardVM_pop_discard( vm ); // context

  BardGL_clear( aaRRggBB );
}


//=============================================================================
//  Image
//=============================================================================
void BardVMImage__native_draw__Integer_Real_Real_Real_Real_Real_Real_Real_Real_Real( BardVM* vm )
{
  double radians   = BardVM_pop_real( vm );
  double uv_height = BardVM_pop_real( vm );
  double uv_width  = BardVM_pop_real( vm );
  double v1        = BardVM_pop_real( vm );
  double u1        = BardVM_pop_real( vm );
  double height    = BardVM_pop_real( vm );
  double width     = BardVM_pop_real( vm );
  double y         = BardVM_pop_real( vm );
  double x         = BardVM_pop_real( vm );
  int texture_id   = BardVM_pop_integer( vm );
  BardVM_pop_discard( vm );  // discard Image context

  BardGL_use_texture( texture_id, 0 );
  BardGL_draw_box_rotated( x, y, width, height, u1, v1, u1+uv_width, v1+uv_height, 0xFFffFFff, radians );
}


//=============================================================================
//  Texture
//=============================================================================
void BardVMTexture__native_load__String( BardVM* vm )
{
  BardString* filepath_object = BardVM_pop_string( vm );
  BardType*   type_Texture = BardVM_peek_object( vm )->type; // leave context on stack for now

  char filepath[PATH_MAX];
  int  resource_id;
  BardGLTexture* texture;

  BardString_to_c_string( filepath_object, filepath, PATH_MAX );

  resource_id = BardGL_load_texture( filepath );

  if ( !resource_id )
  {
    vm->sp->integer = 0;  // overwrite context with integer return value
    return;
  }

  texture = BardGL_get_texture( resource_id );

  // Call Texture::set_size with size info
  BardVM_push_integer( vm, texture->image_width );
  BardVM_push_integer( vm, texture->image_height );
  BardVM_push_integer( vm, texture->texture_width );
  BardVM_push_integer( vm, texture->texture_height );
  BardVM_invoke_by_method_signature( vm, type_Texture, "set_size(Integer,Integer,Integer,Integer)" );

  BardVM_push_integer( vm, resource_id );
}

