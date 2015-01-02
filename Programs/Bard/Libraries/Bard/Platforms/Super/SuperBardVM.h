#ifndef SUPER_BARD_VM_H
#define SUPER_BARD_VM_H

#include "Super.h"
#include "Bard.h"

BARD_BEGIN_HEADER

#ifdef SUPER_PLATFORM_WINDOWS
#include <windows.h>
#endif


//=============================================================================
//  FUNCTIONS
//=============================================================================
void    SuperBardVM_set_args( int argc, const char* argv[] );

BardVM* SuperBardVM_create();
// Creates VM, calls configure().  Uses args defined in SuperBardVM_set_args().

BardVM* SuperBardVM_destroy( BardVM* vm );
// Cleans up and frees the VM if vm != NULL.  Returns NULL.

void    SuperBardVM_configure( BardVM* vm );
// Already called as part of SuperBardVM_create().

int     SuperBardVM_execute( BardVM* vm, const char* filepath );

void    SuperBardVM_launch( BardVM* vm, const char* bc_data );
// Creates the main object.

//void    SuperBardVM_launch_windows( const char* bc_filepath );

#ifdef SUPER_PLATFORM_WINDOWS
void SuperBardVM_configure_windows( HINSTANCE hInstance, HINSTANCE hPrevInstance, const char* lpCmdLine, int nCmdShow );
#endif

BARD_END_HEADER

#endif // SUPER_BARD_VM_H
