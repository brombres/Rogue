#ifndef SUPER_FILE_SYSTEM_H
#define SUPER_FILE_SYSTEM_H

#include "Super.h"

SUPER_BEGIN_HEADER

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================
char* SuperFileSystem_load( const char* filepath );
int   SuperFileSystem_size( const char* filepath );

SUPER_END_HEADER

#endif // SUPER_FILE_SYSTEM_H
