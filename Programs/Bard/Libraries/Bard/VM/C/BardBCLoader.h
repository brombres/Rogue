#ifndef BARD_VM_LOADER_H
#define BARD_VM_LOADER_H

#include "Bard.h"

#define BARD_VM_CURRENT_BC_VERSION 0x20130601

struct BardVM;
struct BardMethod;

int BardBCLoader_load_bc_file( struct BardVM* vm, const char* filename );
int BardBCLoader_load_bc_data( struct BardVM* vm, const char* filename, char* data, int free_data );

int BardBCLoader_load_bc_from_reader( struct BardVM* vm, BardFileReader* reader );

int                BardBCLoader_load_count( struct BardVM* vm, BardFileReader* reader );
char*              BardBCLoader_load_indexed_id( struct BardVM* vm, BardFileReader* reader );
BardType*          BardBCLoader_load_indexed_type( struct BardVM* vm, BardFileReader* reader );
struct BardMethod* BardBCLoader_load_indexed_method( struct BardVM* vm, BardFileReader* reader );

#endif // BARD_VM_LOADER_H
