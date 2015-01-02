#ifndef SUPER_C_STRING_BUILDER
#define SUPER_C_STRING_BUILDER

#include "SuperC.h"

typedef struct SuperCStringBuilder
{
  SuperC*         super_c;
  unsigned short* characters;
  int             count;
  int             capacity;
} SuperCStringBuilder;

SuperCStringBuilder* SuperCStringBuilder_create( SuperC* super_c );
SuperCStringBuilder* SuperCStringBuilder_destroy( SuperCStringBuilder* buffer );

SuperCStringBuilder* SuperCStringBuilder_ensure_additional_capacity( SuperCStringBuilder* buffer, int additional_capacity );
SuperCStringBuilder* SuperCStringBuilder_ensure_capacity( SuperCStringBuilder* buffer, int minimum_capacity );

SuperCStringBuilder* SuperCStringBuilder_print_character( SuperCStringBuilder* buffer, int ch );

unsigned short* SuperCStringBuilder_to_string( SuperCStringBuilder* buffer );
char* SuperCStringBuilder_to_c_string( SuperCStringBuilder* buffer );

#endif // SUPER_C_STRING_BUILDER
