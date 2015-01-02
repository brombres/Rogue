#ifndef BARD_VM_PROCESSOR_H
#define BARD_VM_PROCESSOR_H

#include "BardXC.h"

typedef struct BardProcessor
{
  BardVM* vm;
} BardProcessor;

BardProcessor* BardProcessor_create( BardVM* vm );
BardProcessor* BardProcessor_destroy( BardProcessor* program );

void BardProcessor_on_unhandled_native_method( BardVM* vm );

void BardProcessor_invoke( BardVM* vm, BardType* type, BardMethod* m );
void BardProcessor_execute( BardVM* vm );
void BardProcessor_throw_exception_on_stack( BardVM* vm );

#endif // BARD_VM_PROCESSOR_H
