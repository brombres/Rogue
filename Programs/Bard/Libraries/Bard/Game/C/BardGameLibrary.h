#ifndef BARD_GAME_LIBRARY_H
#define BARD_GAME_LIBRARY_H

#include "Bard.h"

void BardGameLibrary_configure( BardVM* vm );

void BardVMDisplay__clear__Integer( BardVM* vm );

void BardVMImage__native_draw__Integer_Real_Real_Real_Real_Real_Real_Real_Real_Real( BardVM* vm );

void BardVMTexture__native_load__String( BardVM* vm );

#endif // BARD_GAME_LIBRARY_H
