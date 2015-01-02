#ifndef BARD_UI_LIBRARY_WINDOWS_H
#define BARD_UI_LIBRARY_WINDOWS_H

#include "Bard.h"

#ifdef __cplusplus
extern "C"
{
#endif

//=============================================================================
//  configure()
//=============================================================================
void BardUILibrary_configure( BardVM* vm, HINSTANCE h_instance );


//=============================================================================
//  UICheckbox
//=============================================================================
void BardUICheckbox__native_get_value__Integer( BardVM* vm );
void BardUICheckbox__native_set_value__Integer__Logical( BardVM* vm );



//=============================================================================
//  Display
//=============================================================================
void BardUI_update_display_info( BardVM* vm );




#ifdef __cplusplus
}
#endif

#endif // BARD_UI_LIBRARY_WINDOWS_H
