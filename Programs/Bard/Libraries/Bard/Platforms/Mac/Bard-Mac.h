#ifndef BARD_MAC_H
#define BARD_MAC_H

#import <Cocoa/Cocoa.h>
#import "Bard.h"

extern BardVM* Bard_vm;

BardVM* Bard_init();
bool    Bard_load( NSString* program_name );
    //BardVM_free( vm );

void BardUILibrary_configure( BardVM* vm );

NSString* BardString_to_ns_string( BardObject* string_obj );

#endif // BARD_MAC_H
